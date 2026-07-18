// Copyright 2026 The Chromium Authors
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
#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/ref_counted.h"
#include "base/process/memory.h"
#include "base/run_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "net/base/auth.h"
#include "net/base/io_buffer.h"
#include "net/base/net_errors.h"
#include "net/base/proxy_chain.h"
#include "net/base/proxy_server.h"
#include "net/cert/mock_cert_verifier.h"
#include "net/http/http_network_session.h"
#include "net/http/http_auth.h"
#include "net/http/http_auth_cache.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_transaction_factory.h"
#include "net/log/net_log.h"
#include "net/log/file_net_log_observer.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/quic/quic_context.h"
#include "net/quic/quic_proxy_datagram_client_socket.h"
#include "net/quic/quic_session_pool.h"
#include "net/socket/datagram_client_socket.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_error_codes.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"
#include "net/tools/naive/naive_connect_udp_tunnel.h"
#include "net/tools/naive/naive_protocol.h"
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
    net::DefineNetworkTrafficAnnotation("naive_connect_udp_runner", "");
constexpr int kReadBufferSize = 64 * 1024;
constexpr base::TimeDelta kOperationTimeout = base::Seconds(10);

void VerifyConnectUdpUrlConstruction() {
  const net::ProxyServer ipv6_proxy = net::ProxyServer::FromSchemeHostAndPort(
      net::ProxyServer::SCHEME_QUIC, "::1", uint16_t{19661});
  const GURL ipv6_url =
      net::QuicProxyDatagramClientSocket::BuildConnectUdpUrl(
          ipv6_proxy, net::HostPortPair("::1", 53));
  CHECK_EQ(ipv6_url.spec(),
           "https://[::1]:19661/.well-known/masque/udp/"
           "%3A%3A1/53/");
  std::cout << "CONNECT_UDP_URL_CONSTRUCTION_OK" << std::endl;
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

  auto cert_verifier = std::make_unique<net::MockCertVerifier>();
  cert_verifier->set_default_result(net::OK);
  builder.SetCertVerifier(std::move(cert_verifier));
  builder.set_proxy_delegate(std::make_unique<net::NaiveProxyDelegate>(
      net::HttpRequestHeaders(),
      std::vector<net::PaddingType>{net::PaddingType::kVariant1,
                                    net::PaddingType::kNone}));

  // QuicSessionPool copies QuicParams when the context is built. Configure
  // the controlled endpoint before Build() so the test-only self-signed root
  // exception reaches Chromium's proof verifier.
  auto quic_context = std::make_unique<net::QuicContext>();
  auto* quic_params = quic_context->params();
  quic_params->supported_versions = {quic::ParsedQuicVersion::RFCv1()};
  const net::HostPortPair& proxy = proxy_chain.Last().host_port_pair();
  quic_params->origins_to_force_quic_on.insert(url::SchemeHostPort(
      url::kHttpsScheme, proxy.host(), proxy.port()));
  builder.set_quic_context(std::move(quic_context));
  auto context = builder.Build();
  if (!proxy_user.empty()) {
    auto* session = context->http_transaction_factory()->GetSession();
    session->http_auth_cache()->Add(
        url::SchemeHostPort(
            GURL("https://" + proxy_chain.Last().host_port_pair().ToString())),
        net::HttpAuth::AUTH_PROXY, /*realm=*/{},
        net::HttpAuth::AUTH_SCHEME_BASIC, /*network_anonymization_key=*/{},
        /*challenge=*/"Basic",
        net::AuthCredentials(base::UTF8ToUTF16(proxy_user),
                             base::UTF8ToUTF16(proxy_pass)),
        /*path=*/"/");
  }
  return context;
}

class ConnectUdpRunner {
 public:
  enum class Mode {
    kEcho,
    kDestroyWithPendingRead,
    kDestroyWhileConnectPending,
    kShutdownSessionWithPendingRead,
  };

  ConnectUdpRunner(net::HttpNetworkSession* session,
                   const net::ProxyChain& proxy_chain,
                   const net::HostPortPair& target,
                   std::string payload,
                   Mode mode)
      : payload_(std::move(payload)),
        mode_(mode),
        session_(session),
        tunnel_(std::make_unique<net::NaiveConnectUdpTunnel>(
            session,
            proxy_chain,
            net::NetworkAnonymizationKey(),
            target,
            net::NetLogWithSource::Make(session->net_log(),
                                        net::NetLogSourceType::NONE),
            kTrafficAnnotation)) {}

  int Run() {
    timeout_.Start(FROM_HERE, kOperationTimeout, this,
                   &ConnectUdpRunner::OnTimeout);
    int result = tunnel_->Start(base::BindOnce(
        &ConnectUdpRunner::OnTunnelComplete, base::Unretained(this)));
    if (result == net::ERR_IO_PENDING &&
        mode_ == Mode::kDestroyWhileConnectPending) {
      std::cout << "CONNECT_PENDING" << std::endl;
      lifecycle_action_timer_.Start(
          FROM_HERE, base::Milliseconds(500), this,
          &ConnectUdpRunner::DestroyWhileConnectPending);
    }
    if (result != net::ERR_IO_PENDING) {
      OnTunnelComplete(result);
    }
    run_loop_.Run();
    return exit_code_;
  }

 private:
  void OnTunnelComplete(int result) {
    if (mode_ == Mode::kDestroyWhileConnectPending) {
      Fail("CONNECT_COMPLETED_BEFORE_DESTRUCTION", result);
      return;
    }
    if (result != net::OK) {
      Fail("CONNECT_UDP_FAILED", result);
      return;
    }
    std::cout << "CONNECT_UDP_OK" << std::endl;

    if (mode_ == Mode::kDestroyWithPendingRead ||
        mode_ == Mode::kShutdownSessionWithPendingRead) {
      read_buffer_ =
          base::MakeRefCounted<net::IOBufferWithSize>(kReadBufferSize);
      result = tunnel_->socket()->Read(
          read_buffer_.get(), read_buffer_->size(),
          base::BindOnce(&ConnectUdpRunner::OnUnexpectedReadComplete,
                         base::Unretained(this)));
      if (result != net::ERR_IO_PENDING) {
        Fail("PENDING_READ_SETUP_FAILED", result);
        return;
      }
      if (mode_ == Mode::kShutdownSessionWithPendingRead) {
        session_->quic_session_pool()->CloseAllSessions(
            net::ERR_ABORTED, quic::QUIC_CONNECTION_CANCELLED);
        std::cout << "SESSION_SHUTDOWN_ISSUED" << std::endl;
      }
      tunnel_.reset();
      destruction_grace_timer_.Start(
          FROM_HERE, base::Milliseconds(200), this,
          mode_ == Mode::kShutdownSessionWithPendingRead
              ? &ConnectUdpRunner::OnSessionShutdownGraceComplete
              : &ConnectUdpRunner::OnPendingReadDestructionGraceComplete);
      return;
    }

    write_buffer_ = base::MakeRefCounted<net::StringIOBuffer>(payload_);
    result = tunnel_->socket()->Write(
        write_buffer_.get(), static_cast<int>(payload_.size()),
        base::BindOnce(&ConnectUdpRunner::OnWriteComplete,
                       base::Unretained(this)),
        kTrafficAnnotation);
    if (result != net::ERR_IO_PENDING) {
      OnWriteComplete(result);
    }
  }

  void OnWriteComplete(int result) {
    if (result != static_cast<int>(payload_.size())) {
      Fail("DATAGRAM_WRITE_FAILED", result);
      return;
    }
    std::cout << "DATAGRAM_WRITE_OK bytes=" << result << std::endl;

    read_buffer_ =
        base::MakeRefCounted<net::IOBufferWithSize>(kReadBufferSize);
    result = tunnel_->socket()->Read(
        read_buffer_.get(), read_buffer_->size(),
        base::BindOnce(&ConnectUdpRunner::OnReadComplete,
                       base::Unretained(this)));
    if (result != net::ERR_IO_PENDING) {
      OnReadComplete(result);
    }
  }

  void OnReadComplete(int result) {
    if (result < 0) {
      Fail("DATAGRAM_READ_FAILED", result);
      return;
    }
    std::string received(read_buffer_->data(), result);
    if (received != payload_) {
      std::cerr << "DATAGRAM_ECHO_MISMATCH bytes=" << result << std::endl;
      Finish(EXIT_FAILURE);
      return;
    }
    std::cout << "DATAGRAM_ECHO_OK bytes=" << result
              << " payload=" << received << std::endl;
    Finish(EXIT_SUCCESS);
  }

  void OnTimeout() {
    std::cerr << "CONNECT_UDP_RUNNER_TIMEOUT" << std::endl;
    Finish(EXIT_FAILURE);
  }

  void OnUnexpectedReadComplete(int result) {
    Fail("PENDING_READ_CALLBACK_AFTER_DESTRUCTION", result);
  }

  void DestroyWhileConnectPending() {
    tunnel_.reset();
    destruction_grace_timer_.Start(
        FROM_HERE, base::Milliseconds(200), this,
        &ConnectUdpRunner::OnConnectPendingDestructionGraceComplete);
  }

  void OnPendingReadDestructionGraceComplete() {
    std::cout << "PENDING_READ_DESTRUCTION_OK" << std::endl;
    Finish(EXIT_SUCCESS);
  }

  void OnConnectPendingDestructionGraceComplete() {
    std::cout << "CONNECT_PENDING_DESTRUCTION_OK" << std::endl;
    Finish(EXIT_SUCCESS);
  }

  void OnSessionShutdownGraceComplete() {
    std::cout << "SESSION_SHUTDOWN_DESTRUCTION_OK" << std::endl;
    Finish(EXIT_SUCCESS);
  }

  void Fail(const char* marker, int result) {
    std::cerr << marker << " result=" << result
              << " error=" << net::ErrorToShortString(result)
              << " quic_error="
              << quic::QuicErrorCodeToString(
                     tunnel_ ? tunnel_->net_error_details().quic_connection_error
                             : quic::QUIC_NO_ERROR)
              << std::endl;
    Finish(EXIT_FAILURE);
  }

  void Finish(int exit_code) {
    if (finished_) {
      return;
    }
    finished_ = true;
    exit_code_ = exit_code;
    timeout_.Stop();
    run_loop_.Quit();
  }

  const std::string payload_;
  const Mode mode_;
  const raw_ptr<net::HttpNetworkSession> session_;
  std::unique_ptr<net::NaiveConnectUdpTunnel> tunnel_;
  scoped_refptr<net::StringIOBuffer> write_buffer_;
  scoped_refptr<net::IOBufferWithSize> read_buffer_;
  base::OneShotTimer timeout_;
  base::OneShotTimer lifecycle_action_timer_;
  base::OneShotTimer destruction_grace_timer_;
  base::RunLoop run_loop_;
  bool finished_ = false;
  int exit_code_ = EXIT_FAILURE;
};

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
      "naive_connect_udp_runner");

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  base::allocator::PartitionAllocSupport::Get()->ReconfigureAfterTaskRunnerInit(
      process_type);
#endif

  url::AddStandardScheme("quic",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);
  VerifyConnectUdpUrlConstruction();

  const auto& args = base::CommandLine::ForCurrentProcess()->GetArgs();
  if (args.size() != 5) {
    std::cerr << "Usage: naive_connect_udp_runner <proxy-host> <proxy-port> "
                 "<target-host> <target-port> <payload> "
                 "[--proxy-user=...] [--proxy-pass=...] "
                 "[--destroy-with-pending-read | "
                 "--destroy-while-connect-pending | "
                 "--shutdown-session-with-pending-read] "
                 "[--log-net-log=path]"
              << std::endl;
    return EXIT_FAILURE;
  }

  int proxy_port = 0;
  int target_port = 0;
  if (!base::StringToInt(args[1], &proxy_port) || proxy_port <= 0 ||
      proxy_port > 65535 || !base::StringToInt(args[3], &target_port) ||
      target_port <= 0 || target_port > 65535 || args[4].empty()) {
    std::cerr << "INVALID_ARGUMENT" << std::endl;
    return EXIT_FAILURE;
  }

  net::ProxyChain proxy_chain = net::ProxyChain::FromSchemeHostAndPort(
      net::ProxyServer::SCHEME_QUIC, args[0],
      static_cast<uint16_t>(proxy_port));
  if (!proxy_chain.IsValid()) {
    std::cerr << "INVALID_PROXY" << std::endl;
    return EXIT_FAILURE;
  }

  const base::CommandLine& command_line =
      *base::CommandLine::ForCurrentProcess();
  const std::string proxy_user =
      command_line.GetSwitchValueASCII("proxy-user");
  const std::string proxy_pass =
      command_line.GetSwitchValueASCII("proxy-pass");
  if (proxy_user.empty() != proxy_pass.empty()) {
    std::cerr << "proxy-user and proxy-pass must be set together" << std::endl;
    return EXIT_FAILURE;
  }

  std::unique_ptr<net::FileNetLogObserver> net_log_observer;
  const std::string net_log_path =
      command_line.GetSwitchValueASCII("log-net-log");
  if (!net_log_path.empty()) {
    net_log_observer = net::FileNetLogObserver::CreateUnbounded(
        base::FilePath::FromUTF8Unsafe(net_log_path),
        net::NetLogCaptureMode::kDefault, nullptr);
    net_log_observer->StartObserving(net::NetLog::Get());
  }

  auto context = BuildRunnerContext(proxy_chain, proxy_user, proxy_pass);
  auto* session = context->http_transaction_factory()->GetSession();
  std::cout << "SESSION_READY proxy=" << proxy_chain.ToDebugString()
            << std::endl;

  int lifecycle_mode_count = 0;
  lifecycle_mode_count +=
      command_line.HasSwitch("destroy-with-pending-read") ? 1 : 0;
  lifecycle_mode_count +=
      command_line.HasSwitch("destroy-while-connect-pending") ? 1 : 0;
  lifecycle_mode_count +=
      command_line.HasSwitch("shutdown-session-with-pending-read") ? 1 : 0;
  if (lifecycle_mode_count > 1) {
    std::cerr << "lifecycle test switches are mutually exclusive" << std::endl;
    return EXIT_FAILURE;
  }

  ConnectUdpRunner::Mode mode = ConnectUdpRunner::Mode::kEcho;
  if (command_line.HasSwitch("destroy-with-pending-read")) {
    mode = ConnectUdpRunner::Mode::kDestroyWithPendingRead;
  } else if (command_line.HasSwitch("destroy-while-connect-pending")) {
    mode = ConnectUdpRunner::Mode::kDestroyWhileConnectPending;
  } else if (command_line.HasSwitch("shutdown-session-with-pending-read")) {
    mode = ConnectUdpRunner::Mode::kShutdownSessionWithPendingRead;
  }

  ConnectUdpRunner runner(
      session, proxy_chain,
      net::HostPortPair(args[2], static_cast<uint16_t>(target_port)), args[4],
      mode);
  const int result = runner.Run();

  if (net_log_observer) {
    base::RunLoop flush_loop;
    net_log_observer->StopObserving(nullptr, flush_loop.QuitClosure());
    flush_loop.Run();
    std::cout << "NET_LOG_WRITTEN path=" << net_log_path << std::endl;
  }
  return result;
}
