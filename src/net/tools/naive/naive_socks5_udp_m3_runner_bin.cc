// Copyright 2026 The NaiveProxy Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "base/at_exit.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/functional/bind.h"
#include "base/process/memory.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "net/base/auth.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_chain.h"
#include "net/cert/mock_cert_verifier.h"
#include "net/http/http_auth.h"
#include "net/http/http_auth_cache.h"
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
#include "net/tools/naive/naive_connect_udp_backend_factory.h"
#include "net/tools/naive/naive_protocol.h"
#include "net/tools/naive/naive_proxy.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_builder.h"
#include "url/gurl.h"
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
    net::DefineNetworkTrafficAnnotation("naive_socks5_udp_m3_runner", "");

void VerifyProductionFactoryEligibility(
    net::HttpNetworkSession* session,
    const net::ProxyChain& all_quic_chain) {
  const net::NetworkAnonymizationKey nak =
      net::NetworkAnonymizationKey::CreateTransient();
  auto create_for = [&](const net::ProxyChain& chain) {
    return net::CreateNaiveConnectUdpDatagramBackend(
        net::Socks5UdpBackendContext(
            /*association_id=*/999, session, chain, nak,
            net::NetLogWithSource(), kTrafficAnnotation, base::Seconds(10),
            base::Seconds(30)));
  };
  CHECK(create_for(all_quic_chain));
  CHECK(!create_for(net::ProxyChain::Direct()));
  const net::ProxyServer https = net::ProxyServer::FromSchemeHostAndPort(
      net::ProxyServer::SCHEME_HTTPS, "127.0.0.1", uint16_t{9});
  CHECK(!create_for(net::ProxyChain(https)));
  const net::ProxyChain mixed = net::ProxyChain::ForIpProtection(
      {all_quic_chain.First(), https});
  CHECK(mixed.IsValid());
  CHECK(!create_for(mixed));
  std::cout << "M3_PRODUCTION_FACTORY_ELIGIBILITY_OK" << std::endl;
}

std::unique_ptr<net::URLRequestContext> BuildRunnerContext(
    const net::ProxyChain& proxy_chain,
    const std::string& proxy_user,
    const std::string& proxy_pass) {
  net::URLRequestContextBuilder builder;
  builder.DisableHttpCache();
  builder.set_net_log(net::NetLog::Get());

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

  // Test-only certificate bypass. Production naive_proxy_bin.cc continues to
  // use CertVerifier::CreateDefault(). This must be installed before Build().
  auto cert_verifier = std::make_unique<net::MockCertVerifier>();
  cert_verifier->set_default_result(net::OK);
  builder.SetCertVerifier(std::move(cert_verifier));
  builder.set_proxy_delegate(std::make_unique<net::NaiveProxyDelegate>(
      net::HttpRequestHeaders(),
      std::vector<net::PaddingType>{net::PaddingType::kVariant1,
                                    net::PaddingType::kNone}));

  auto quic_context = std::make_unique<net::QuicContext>();
  quic_context->params()->supported_versions = {
      quic::ParsedQuicVersion::RFCv1()};
  const net::HostPortPair& proxy = proxy_chain.Last().host_port_pair();
  quic_context->params()->origins_to_force_quic_on.insert(
      url::SchemeHostPort(url::kHttpsScheme, proxy.host(), proxy.port()));
  builder.set_quic_context(std::move(quic_context));

  auto context = builder.Build();
  if (!proxy_user.empty()) {
    auto* session = context->http_transaction_factory()->GetSession();
    session->http_auth_cache()->Add(
        url::SchemeHostPort(
            GURL("https://" + proxy_chain.Last().host_port_pair().ToString())),
        net::HttpAuth::AUTH_PROXY, /*realm=*/{},
        net::HttpAuth::AUTH_SCHEME_BASIC,
        /*network_anonymization_key=*/{}, /*challenge=*/"Basic",
        net::AuthCredentials(base::UTF8ToUTF16(proxy_user),
                             base::UTF8ToUTF16(proxy_pass)),
        /*path=*/"/");
  }
  return context;
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
      "naive_socks5_udp_m3_runner");

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  base::allocator::PartitionAllocSupport::Get()->ReconfigureAfterTaskRunnerInit(
      process_type);
#endif

  url::AddStandardScheme("quic",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);
  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  const std::string proxy_host =
      command_line.GetSwitchValueASCII("proxy-host");
  unsigned int proxy_port_value = 0;
  if (proxy_host.empty() ||
      !base::StringToUint(command_line.GetSwitchValueASCII("proxy-port"),
                         &proxy_port_value) ||
      proxy_port_value == 0 || proxy_port_value > 65535) {
    std::cerr << "Usage: naive_socks5_udp_m3_runner --proxy-host=HOST "
                 "--proxy-port=PORT [--proxy-user=USER --proxy-pass=PASS]"
              << std::endl;
    return EXIT_FAILURE;
  }
  const std::string proxy_user =
      command_line.GetSwitchValueASCII("proxy-user");
  const std::string proxy_pass =
      command_line.GetSwitchValueASCII("proxy-pass");
  if (proxy_user.empty() != proxy_pass.empty()) {
    std::cerr << "proxy-user and proxy-pass must be set together" << std::endl;
    return EXIT_FAILURE;
  }
  const std::string listen_host =
      command_line.HasSwitch("listen-host")
          ? command_line.GetSwitchValueASCII("listen-host")
          : "127.0.0.1";
  const std::string listen_user =
      command_line.GetSwitchValueASCII("listen-user");
  const std::string listen_pass =
      command_line.GetSwitchValueASCII("listen-pass");

  const net::ProxyChain proxy_chain =
      net::ProxyChain::FromSchemeHostAndPort(
          net::ProxyServer::SCHEME_QUIC, proxy_host,
          static_cast<uint16_t>(proxy_port_value));
  CHECK(proxy_chain.IsValid());

  // Declaration order is intentional: proxy and every backend/tunnel are
  // destroyed before the URLRequestContext/session they reference.
  auto context = BuildRunnerContext(proxy_chain, proxy_user, proxy_pass);
  auto* session = context->http_transaction_factory()->GetSession();
  VerifyProductionFactoryEligibility(session, proxy_chain);
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

  auto proxy = std::make_unique<net::NaiveProxy>(
      std::move(listen_socket), net::ClientProtocol::kSocks5, listen_user,
      listen_pass, /*concurrency=*/1, /*tunnel_timeout=*/30,
      /*idle_timeout=*/30, /*resolver=*/nullptr, session, kTrafficAnnotation,
      std::vector<net::PaddingType>{net::PaddingType::kVariant1,
                                    net::PaddingType::kNone},
      base::BindRepeating(&net::CreateNaiveConnectUdpDatagramBackend));

  std::cout << "M3_SOCKS5_UDP_READY host=" << listen_host
            << " port=" << listen_endpoint.port()
            << " proxy=" << proxy_host << ":" << proxy_port_value
            << " auth=" << (proxy_user.empty() ? "disabled" : "cached")
            << std::endl;

  base::RunLoop run_loop;
  base::OneShotTimer lifetime_timer;
  unsigned int run_for_ms = 0;
  if (command_line.HasSwitch("run-for-ms")) {
    CHECK(base::StringToUint(command_line.GetSwitchValueASCII("run-for-ms"),
                             &run_for_ms));
    CHECK_GT(run_for_ms, 0u);
    lifetime_timer.Start(FROM_HERE, base::Milliseconds(run_for_ms),
                         run_loop.QuitClosure());
  }
  run_loop.Run();
  proxy.reset();
  std::cout << "M3_RUNNER_PROXY_DESTROYED_BEFORE_CONTEXT" << std::endl;
  return EXIT_SUCCESS;
}
