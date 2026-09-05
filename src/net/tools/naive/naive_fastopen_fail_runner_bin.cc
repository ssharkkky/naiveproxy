// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Fast Open async-failure runner (test-only fixture).
//
// Drives two sequential TCP tunnel exchanges through a quic:// proxy chain
// with the real production NaiveProxyDelegate:
//
//   Exchange 1: the proxy's padding support has not been negotiated yet, so
//       the client does not enable Fast Open. Connect() blocks until the
//       controlled MASQUE server (run with --fail_connects) delivers the
//       502, then fails with the tunnel error. The delegate learns the
//       server's (absent) padding support from the parsed CONNECT response.
//
//   Exchange 2: the padding state is now known, so the client enables Fast
//       Open: Connect() completes before the response arrives and the
//       application I/O (early data write and/or data read) is pending. The
//       server answers the same delayed 502 without FIN, leaving the stream
//       open. The pending application I/O must observe the failure with an
//       error instead of hanging indefinitely.
//
// Exit code 0: exchange 1 failed as expected, padding was learned, and
// exchange 2 completed with an error within the watchdog.
// Exit code 1: any hang, unexpected success, or setup failure.

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
#include "base/task/single_thread_task_executor.h"
#include "base/task/thread_pool/thread_pool_instance.h"
#include "base/timer/timer.h"
#include "build/build_config.h"
#include "net/base/io_buffer.h"
#include "net/base/load_flags.h"
#include "net/base/net_errors.h"
#include "net/base/privacy_mode.h"
#include "net/base/proxy_chain.h"
#include "net/base/proxy_server.h"
#include "net/cert/mock_cert_verifier.h"
#include "net/http/http_request_headers.h"
#include "net/http/http_auth.h"
#include "net/http/http_transaction_factory.h"
#include "net/base/request_priority.h"
#include "net/log/net_log.h"
#include "net/log/net_log_capture_mode.h"
#include "net/log/net_log_with_source.h"
#include "net/proxy_resolution/configured_proxy_resolution_service.h"
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_config_with_annotation.h"
#include "net/proxy_resolution/proxy_info.h"
#include "net/quic/quic_context.h"
#include "net/socket/client_socket_handle.h"
#include "net/socket/client_socket_pool_manager.h"
#include "net/third_party/quiche/src/quiche/quic/core/quic_versions.h"
#include "net/tools/naive/naive_proxy_delegate.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/url_request/url_request.h"
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
    net::DefineNetworkTrafficAnnotation("naive_fastopen_fail_runner", "");
constexpr int kReadBufferSize = 64 * 1024;
// Exchange 1 must complete quickly (502 with FIN). Exchange 2 must fail
// promptly after the server's delayed 502 arrives (500 ms delay on the
// server). 15 s is far beyond any of those and well below a hang.
constexpr base::TimeDelta kExchangeWatchdog = base::Seconds(15);

// Preserve the F1 regression independently of production's CONNECT policy.
class LegacyFastOpenDelegate : public net::NaiveProxyDelegate {
 public:
  using net::NaiveProxyDelegate::NaiveProxyDelegate;

  base::expected<net::HttpRequestHeaders, net::Error> OnBeforeTunnelRequest(
      const net::ProxyChain& chain,
      size_t index,
      OnBeforeTunnelRequestCallback callback) override {
    auto headers = net::NaiveProxyDelegate::OnBeforeTunnelRequest(
        chain, index, std::move(callback));
    if (headers.has_value() && GetProxyChainPaddingType(chain).has_value()) {
      headers->SetHeader("fastopen", "1");
    }
    return headers;
  }
};

std::unique_ptr<net::URLRequestContext> BuildRunnerContext(
    const net::ProxyChain& proxy_chain, bool standard_connect) {
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
  const std::vector<net::PaddingType> padding_types{
      net::PaddingType::kVariant1, net::PaddingType::kNone};
  if (standard_connect) {
    builder.set_proxy_delegate(std::make_unique<net::NaiveProxyDelegate>(
        net::HttpRequestHeaders(), padding_types));
  } else {
    builder.set_proxy_delegate(std::make_unique<LegacyFastOpenDelegate>(
        net::HttpRequestHeaders(), padding_types));
  }

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
  return builder.Build();
}

int RunStandardConnect(net::URLRequestContext* context,
                       const net::ProxyChain& proxy_chain,
                       const std::string& target_host,
                       int target_port,
                       bool expect_success) {
  auto* session = context->http_transaction_factory()->GetSession();
  net::ProxyInfo proxy_info;
  proxy_info.UseProxyChain(proxy_chain);
  for (int exchange = 1; exchange <= 2; ++exchange) {
    net::ClientSocketHandle handle;
    base::RunLoop loop;
    base::OneShotTimer watchdog;
    int result = net::ERR_IO_PENDING;
    int callbacks = 0;
    watchdog.Start(
        FROM_HERE, kExchangeWatchdog,
        base::BindOnce(
            [](int* result, base::RepeatingClosure quit) {
              *result = net::ERR_TIMED_OUT;
              quit.Run();
            },
            &result, loop.QuitClosure()));
    const auto started = base::TimeTicks::Now();
    result = net::InitSocketHandleForHttpRequest(
        url::SchemeHostPort("http", target_host, target_port),
        net::LOAD_IGNORE_LIMITS, net::MAXIMUM_PRIORITY, session, proxy_info, {},
        net::PRIVACY_MODE_DISABLED,
        net::NetworkAnonymizationKey::CreateTransient(),
        net::SecureDnsPolicy::kDisable, net::SocketTag(),
        net::handles::kInvalidNetworkHandle, net::NetLogWithSource(), &handle,
        base::BindOnce(
            [](int* callbacks, int* result, base::RepeatingClosure quit,
               int rv) {
              ++*callbacks;
              *result = rv;
              quit.Run();
            },
            &callbacks, &result, loop.QuitClosure()),
        net::ClientSocketPool::ProxyAuthCallback());
    if (result == net::ERR_IO_PENDING) {
      loop.Run();
    }
    watchdog.Stop();
    const auto elapsed = base::TimeTicks::Now() - started;
    handle.ResetAndCloseSocket();
    const int expected = expect_success ? net::OK
                                        : net::ERR_TUNNEL_CONNECTION_FAILED;
    if (result != expected || callbacks != 1 ||
        elapsed < base::Milliseconds(400)) {
      std::cerr << "STANDARD_CONNECT_FAILED exchange=" << exchange
                << " error=" << result << " callbacks=" << callbacks
                << " elapsed_ms=" << elapsed.InMilliseconds() << std::endl;
      return EXIT_FAILURE;
    }
    auto* delegate =
        static_cast<net::NaiveProxyDelegate*>(context->proxy_delegate());
    if (!delegate->GetProxyChainPaddingType(proxy_chain).has_value()) {
      std::cerr << "STANDARD_CONNECT_PADDING_NOT_LEARNED" << std::endl;
      return EXIT_FAILURE;
    }
    std::cout << "STANDARD_CONNECT_RESPONSE_OK exchange=" << exchange
              << " error=" << result << " callbacks=" << callbacks
              << " elapsed_ms=" << elapsed.InMilliseconds() << std::endl;
  }
  std::cout << "STANDARD_CONNECT_OK" << std::endl;
  return EXIT_SUCCESS;
}

class FastOpenFailRunner;

class ExchangeDelegate : public net::URLRequest::Delegate {
 public:
  explicit ExchangeDelegate(FastOpenFailRunner* runner) : runner_(runner) {}

  void OnResponseStarted(net::URLRequest* request, int net_error) override;
  void OnReadCompleted(net::URLRequest* request, int bytes_read) override;

 private:
  raw_ptr<FastOpenFailRunner> runner_;
};

class FastOpenFailRunner {
 public:
  FastOpenFailRunner(net::URLRequestContext* context,
                     const net::ProxyChain& proxy_chain,
                     const std::string& target_host,
                     int target_port)
      : context_(context),
        proxy_chain_(proxy_chain),
        target_url_("http://" + target_host + ":" + std::to_string(target_port) +
                     "/index.html"),
        delegate_(std::make_unique<ExchangeDelegate>(this)) {}

  ~FastOpenFailRunner() { request_.reset(); }

  int Run() {
    StartExchange(1);
    run_loop_.Run();
    return exit_code_;
  }

  // Invoked by ExchangeDelegate for the request it delegates to.
  void OnResponseStarted(net::URLRequest* request, int net_error) {
    if (net_error < 0) {
      OnExchangeComplete(net_error);
      return;
    }
    const int status = request->GetResponseCode();
    if (status != 502) {
      Fail("UNEXPECTED_STATUS", status);
      return;
    }
    // A 502 delivered as the response: drain its (empty) body; completion
    // arrives as EOF or an error read.
    read_buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(kReadBufferSize);
    int rv = request->Read(read_buffer_.get(), read_buffer_->size());
    if (rv != net::ERR_IO_PENDING)
      OnReadCompleted(request, rv);
  }

  void OnReadCompleted(net::URLRequest* request, int bytes_read) {
    if (bytes_read < 0) {
      OnExchangeComplete(bytes_read);
      return;
    }
    if (bytes_read == 0) {
      // The 502 body is exhausted; the CONNECT failure is the tunnel error.
      OnExchangeComplete(net::ERR_TUNNEL_CONNECTION_FAILED);
      return;
    }
    read_buffer_ = base::MakeRefCounted<net::IOBufferWithSize>(kReadBufferSize);
    int rv = request->Read(read_buffer_.get(), read_buffer_->size());
    if (rv == net::ERR_IO_PENDING)
      return;
    // Synchronous completion; re-enter with the result.
    OnReadCompleted(request, rv);
  }

 private:
  void StartExchange(int exchange) {
    exchange_ = exchange;
    std::cout << "EXCHANGE_START n=" << exchange_ << std::endl;
    watchdog_.Start(FROM_HERE, kExchangeWatchdog, this,
                    &FastOpenFailRunner::OnWatchdog);
    request_ = context_->CreateRequest(GURL(target_url_), net::DEFAULT_PRIORITY,
                                       delegate_.get(), kTrafficAnnotation);
    request_->Start();
  }

  void OnExchangeComplete(int net_error) {
    watchdog_.Stop();
    std::cout << "EXCHANGE_COMPLETE n=" << exchange_
              << " error=" << net_error << " "
              << net::ErrorToShortString(net_error) << std::endl;
    if (exchange_ == 1) {
      if (net_error >= 0) {
        Fail("FIRST_EXCHANGE_UNEXPECTEDLY_OK", 0);
        return;
      }
      // Exchange 1 failed on the 502; the delegate must have parsed the
      // CONNECT response and learned the (absent) padding support, so
      // exchange 2 goes through Fast Open.
      auto* proxy_delegate =
          static_cast<net::NaiveProxyDelegate*>(context_->proxy_delegate());
      if (!proxy_delegate) {
        Fail("PROXY_DELEGATE_MISSING", 0);
        return;
      }
      if (!proxy_delegate->GetProxyChainPaddingType(proxy_chain_)
               .has_value()) {
        Fail("PADDING_NOT_LEARNED", 0);
        return;
      }
      std::cout << "PADDING_LEARNED" << std::endl;
      request_.reset();
      StartExchange(2);
      return;
    }

    // Exchange 2: the Fast Open CONNECT response failed asynchronously
    // (delayed 502, stream not FIN'd). The pending application read must
    // have observed the failure instead of hanging.
    if (net_error < 0) {
      std::cout << "FASTOPEN_ASYNC_FAILURE_HANDLED error=" << net_error << " "
                << net::ErrorToShortString(net_error) << std::endl;
      Finish(EXIT_SUCCESS);
      return;
    }
    Fail("FASTOPEN_SECOND_EXCHANGE_UNEXPECTEDLY_OK", 0);
  }

  void OnWatchdog() {
    Fail("FASTOPEN_ASYNC_FAILURE_HANG", -1);
  }

  void Fail(const char* marker, int result) {
    std::cerr << marker << " result=" << result << std::endl;
    Finish(EXIT_FAILURE);
  }

  void Finish(int exit_code) {
    if (finished_) {
      return;
    }
    finished_ = true;
    exit_code_ = exit_code;
    watchdog_.Stop();
    request_.reset();
    run_loop_.Quit();
  }

  const raw_ptr<net::URLRequestContext> context_;
  const net::ProxyChain proxy_chain_;
  const std::string target_url_;
  std::unique_ptr<ExchangeDelegate> delegate_;
  scoped_refptr<net::IOBufferWithSize> read_buffer_;
  std::unique_ptr<net::URLRequest> request_;
  int exchange_ = 0;
  base::OneShotTimer watchdog_;
  base::RunLoop run_loop_;
  bool finished_ = false;
  int exit_code_ = EXIT_FAILURE;
};

void ExchangeDelegate::OnResponseStarted(net::URLRequest* request,
                                         int net_error) {
  runner_->OnResponseStarted(request, net_error);
}

void ExchangeDelegate::OnReadCompleted(net::URLRequest* request,
                                       int bytes_read) {
  runner_->OnReadCompleted(request, bytes_read);
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
      "naive_fastopen_fail_runner");

#if PA_BUILDFLAG(USE_PARTITION_ALLOC)
  base::allocator::PartitionAllocSupport::Get()->ReconfigureAfterTaskRunnerInit(
      process_type);
#endif

  url::AddStandardScheme("quic",
                         url::SCHEME_WITH_HOST_PORT_AND_USER_INFORMATION);

  const auto& args = base::CommandLine::ForCurrentProcess()->GetArgs();
  if (args.size() != 4) {
    std::cerr << "Usage: naive_fastopen_fail_runner <proxy-host> "
                 "<proxy-port> <target-host> <target-port>"
              << std::endl;
    return EXIT_FAILURE;
  }

  int proxy_port = 0;
  int target_port = 0;
  if (!base::StringToInt(args[1], &proxy_port) || proxy_port <= 0 ||
      proxy_port > 65535 || !base::StringToInt(args[3], &target_port) ||
      target_port <= 0 || target_port > 65535) {
    std::cerr << "INVALID_ARGUMENT" << std::endl;
    return EXIT_FAILURE;
  }

  const auto* command_line = base::CommandLine::ForCurrentProcess();
  const bool standard_connect = command_line->HasSwitch("standard-connect");
  net::ProxyChain proxy_chain = net::ProxyChain::FromSchemeHostAndPort(
      command_line->HasSwitch("https-proxy") ? net::ProxyServer::SCHEME_HTTPS
                                             : net::ProxyServer::SCHEME_QUIC,
      args[0],
      static_cast<uint16_t>(proxy_port));
  if (!proxy_chain.IsValid()) {
    std::cerr << "INVALID_PROXY" << std::endl;
    return EXIT_FAILURE;
  }

  auto context = BuildRunnerContext(proxy_chain, standard_connect);
  std::cout << "SESSION_READY proxy=" << proxy_chain.ToDebugString()
            << std::endl;

  if (standard_connect) {
    return RunStandardConnect(context.get(), proxy_chain, args[2], target_port,
                              command_line->HasSwitch("expect-success"));
  }
  FastOpenFailRunner runner(context.get(), proxy_chain, args[2], target_port);
  return runner.Run();
}
