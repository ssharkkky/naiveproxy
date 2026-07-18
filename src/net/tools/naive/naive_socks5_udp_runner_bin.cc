// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/process/memory.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "build/build_config.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_chain.h"
#include "net/base/proxy_server.h"
#include "net/cert/mock_cert_verifier.h"
#include "net/http/http_network_session.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_transaction_factory.h"
#include "net/log/net_log.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/quic/quic_context.h"
#include "net/socket/tcp_server_socket.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/naive_proxy.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "net/tools/naive/socks5_udp_datagram_backend.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/scheme_host_port.h"
#include "url/url_util.h"

#if BUILDFLAG(IS_APPLE)
#include "base/allocator/early_zone_registration_apple.h"
#include "base/apple/scoped_nsautorelease_pool.h"
#endif

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
#include "base/allocator/allocator_check.h"
#include "base/allocator/partition_alloc_support.h"
#include "base/allocator/partition_allocator/src/partition_alloc/shim/allocator_shim.h"
#endif

#if BUILDFLAG(IS_APPLE) && PA_BUILDFLAG(USE_ALLOCATOR_SHIM)
#include "partition_alloc/shim/allocator_shim.h"
#endif

namespace {

constexpr net::NetworkTrafficAnnotationTag kTrafficAnnotation =
    net::DefineNetworkTrafficAnnotation("naive_socks5_udp_runner", "");

class EchoDatagramBackend final : public net::Socks5UdpDatagramBackend {
 public:
  void Start(ReceiveCallback receive_callback) override {
    receive_callback_ = std::move(receive_callback);
  }

  int Send(net::Socks5UdpDatagram datagram,
           net::CompletionOnceCallback callback) override {
    CHECK(receive_callback_);
    receive_callback_.Run(std::move(datagram));
    return net::OK;
  }

 private:
  ReceiveCallback receive_callback_;
};

std::unique_ptr<net::URLRequestContext> BuildRunnerContext(
    net::ProxyServer::Scheme scheme,
    const std::string& proxy_host,
    uint16_t proxy_port) {
  net::URLRequestContextBuilder builder;
  builder.DisableHttpCache();
  builder.set_net_log(net::NetLog::Get());

  const net::ProxyChain proxy_chain = net::ProxyChain::FromSchemeHostAndPort(
      scheme, proxy_host, proxy_port);
  CHECK(proxy_chain.IsValid());
  net::ProxyConfig proxy_config;
  proxy_config.proxy_rules().type =
      net::ProxyConfig::ProxyRules::Type::PROXY_LIST;
  proxy_config.proxy_rules().single_proxies.SetSingleProxyChain(proxy_chain);
  auto proxy_service =
      net::ConfiguredProxyResolutionService::CreateWithoutProxyResolver(
          std::make_unique<net::ProxyConfigServiceFixed>(
              net::ProxyConfigWithAnnotation(proxy_config,
                                             kTrafficAnnotation)),
          nullptr, net::NetLog::Get());
  proxy_service->ForceReloadProxyConfig();
  builder.set_proxy_resolution_service(std::move(proxy_service));

  auto cert_verifier = std::make_unique<net::MockCertVerifier>();
  cert_verifier->set_default_result(net::OK);
  builder.SetCertVerifier(std::move(cert_verifier));
  builder.set_proxy_delegate(std::make_unique<net::NaiveProxyDelegate>(
      net::HttpRequestHeaders(),
      std::vector<net::PaddingType>{net::PaddingType::kVariant1,
                                    net::PaddingType::kNone}));

  if (scheme == net::ProxyServer::SCHEME_QUIC) {
    auto quic_context = std::make_unique<net::QuicContext>();
    quic_context->params()->supported_versions = {
        quic::ParsedQuicVersion::RFCv1()};
    quic_context->params()->origins_to_force_quic_on.insert(
        url::SchemeHostPort(url::kHttpsScheme, proxy_host, proxy_port));
    builder.set_quic_context(std::move(quic_context));
  }
  return builder.Build();
}

}  // namespace

int main(int argc, char* argv[]) {
#if BUILDFLAG(IS_APPLE)
  partition_alloc::EarlyMallocZoneRegistration();
  base::apple::ScopedNSAutoreleasePool pool;
#endif

#if BUILDFLAG(IS_APPLE) && PA_BUILDFLAG(USE_ALLOCATOR_SHIM)
  allocator_shim::InitializeAllocatorShim();
#endif

  base::EnableTerminationOnOutOfMemory();
  base::CommandLine::Init(argc, argv);
  base::EnableTerminationOnHeapCorruption();
  base::AtExitManager exit_manager;

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  const std::string process_type;
  base::allocator::PartitionAllocSupport::Get()->ReconfigureEarlyish(
      process_type);
  CHECK(base::allocator::IsAllocatorInitialized());
#endif

  base::FeatureList::InitInstance("PartitionConnectionsByNetworkIsolationKey",
                                  std::string());

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  base::allocator::PartitionAllocSupport::Get()
      ->ReconfigureAfterFeatureListInit(process_type);
#endif

  base::SingleThreadTaskExecutor io_task_executor(base::MessagePumpType::IO);
  base::ThreadPoolInstance::CreateAndStartWithDefaultParams(
      "naive_socks5_udp_runner");

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  base::allocator::PartitionAllocSupport::Get()->ReconfigureAfterTaskRunnerInit(
      process_type);
#endif

  url::AddStandardScheme("quic",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  const std::string scheme_name =
      command_line.GetSwitchValueASCII("proxy-scheme");
  const net::ProxyServer::Scheme scheme =
      scheme_name == "http" ? net::ProxyServer::SCHEME_HTTP
                            : net::ProxyServer::SCHEME_QUIC;
  const std::string listen_host =
      command_line.HasSwitch("listen-host")
          ? command_line.GetSwitchValueASCII("listen-host")
          : "127.0.0.1";
  const std::string listen_user =
      command_line.GetSwitchValueASCII("listen-user");
  const std::string listen_pass =
      command_line.GetSwitchValueASCII("listen-pass");
  const std::string proxy_host = listen_host;
  constexpr uint16_t kClosedProxyPort = 9;

  auto context = BuildRunnerContext(scheme, proxy_host, kClosedProxyPort);
  auto* session = context->http_transaction_factory()->GetSession();

  auto listen_socket =
      std::make_unique<net::TCPServerSocket>(net::NetLog::Get(),
                                             net::NetLogSource());
  int result = listen_socket->ListenWithAddressAndPort(listen_host, 0, 16);
  if (result != net::OK) {
    std::cerr << "LISTEN_FAILED result=" << result << std::endl;
    return EXIT_FAILURE;
  }
  net::IPEndPoint listen_endpoint;
  CHECK_EQ(listen_socket->GetLocalAddress(&listen_endpoint), net::OK);

  net::Socks5UdpBackendFactory backend_factory;
  if (scheme == net::ProxyServer::SCHEME_QUIC &&
      scheme_name != "quic-no-backend") {
    backend_factory = base::BindRepeating(
        []() -> std::unique_ptr<net::Socks5UdpDatagramBackend> {
          return std::make_unique<EchoDatagramBackend>();
        });
  }
  auto proxy = std::make_unique<net::NaiveProxy>(
      std::move(listen_socket), net::ClientProtocol::kSocks5,
      listen_user, listen_pass, /*concurrency=*/1,
      /*tunnel_timeout=*/2, /*idle_timeout=*/5, /*resolver=*/nullptr, session,
      kTrafficAnnotation,
      std::vector<net::PaddingType>{net::PaddingType::kVariant1,
                                    net::PaddingType::kNone},
      std::move(backend_factory));
  std::cout << "M2_SOCKS5_UDP_READY host=" << listen_host
            << " port=" << listen_endpoint.port()
            << " scheme=" << (scheme == net::ProxyServer::SCHEME_QUIC
                                    ? "quic"
                                    : "http")
            << std::endl;
  base::RunLoop().Run();
  return EXIT_SUCCESS;
}
