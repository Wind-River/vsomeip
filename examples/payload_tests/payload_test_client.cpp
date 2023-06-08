//
// Copyright (c) 2023 Wind River Systems, Inc.
//
// Copyright (C) 2015-2017 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "payload_test_client.hpp"
#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
#include <csignal>
#include <atomic>
#endif
enum class payloadsize
    : std::uint8_t
    {
        UDS, TCP, UDP, USER_SPECIFIED
};

// this variables are changed via cmdline parameters
static bool use_tcp = false;
static bool call_service_sync = true;
static std::uint32_t sliding_window_size = vsomeip_test::NUMBER_OF_MESSAGES_TO_SEND_PAYLOAD_TESTS;
static payloadsize max_payload_size = payloadsize::UDS;
static bool shutdown_service_at_end = true;
static std::uint32_t user_defined_max_payload;
static std::uint32_t number_of_messages_to_send = 0;
static bool running_ = false;
static bool other_client_exist = false;
static std::atomic<bool> do_not_wait(false);

struct service_info {
    vsomeip::service_t service_id;
    vsomeip::instance_t instance_id;
    vsomeip::method_t method_id;
    };

static constexpr std::array<service_info, 7> service_infos = {{
            { 0xFFFF, 0xFFFF, 0xFFFF },
            { 0x1000, 0x1111, 0x1111 },
            { 0x2000, 0x1111, 0x2222 },
            { 0x3000, 0x2222, 0x3333 },
            { 0x4000, 0x4444, 0x4 },
            { 0x5000, 0x5555, 0x5 },
            { 0x6000, 0x6666, 0x6 }
    }};

payload_test_client::payload_test_client(
        bool _use_tcp,
        bool _call_service_sync,
        std::uint32_t _sliding_window_size) :
                app_(vsomeip::runtime::get()->create_application()),
                request_(vsomeip::runtime::get()->create_request(_use_tcp)),
                call_service_sync_(_call_service_sync),
                sliding_window_size_(_sliding_window_size),
                blocked_(false),
                is_available_(false),
                wait_for_stop_(false),
                number_of_messages_to_send_(number_of_messages_to_send ? number_of_messages_to_send : vsomeip_test::NUMBER_OF_MESSAGES_TO_SEND_PAYLOAD_TESTS),
                number_of_sent_messages_(0),
                number_of_sent_messages_total_(0),
                number_of_acknowledged_messages_(0),
                current_payload_size_(1),
                all_msg_acknowledged_(false),
                sender_(std::bind(&payload_test_client::run, this))
{
}

payload_test_client::~payload_test_client()
{
        stop_thread_.join();
}

#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
payload_test_client *its_sample_ptr(nullptr);
void handle_signal(int _signal) {
    if (its_sample_ptr != nullptr &&
            (_signal == SIGINT || _signal == SIGTERM)) {
                its_sample_ptr->signal_stop();
            }
}
#endif

void payload_test_client::config_service_info()
{
    services_available_[std::make_pair(vsomeip_test::TEST_SERVICE_SERVICE_ID, 
    vsomeip_test::TEST_SERVICE_INSTANCE_ID)] = false;
    for(const struct service_info& i: service_infos) {
        if (i.service_id == 0xFFFF && i.instance_id == 0xFFFF) 
        {
            continue;
        }
        services_available_[std::make_pair(i.service_id, i.instance_id)] = false;
        services_messages_sent[std::make_pair(i.service_id, i.instance_id)] = 0;
        VSOMEIP_INFO << "   service id " << std::hex << i.service_id;
        VSOMEIP_INFO << "   instance id " << std::hex << i.instance_id;
        VSOMEIP_INFO << "   method id " << std::hex << i.method_id;
    }
}

bool payload_test_client::init()
{
    if (!app_->init()) {
        //ADD_FAILURE() << "Couldn't initialize application";
        return false;
    }

    app_->register_state_handler(
            std::bind(&payload_test_client::on_state, this,
                    std::placeholders::_1));

    app_->register_message_handler(vsomeip::ANY_SERVICE,
            vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD,
            std::bind(&payload_test_client::on_message, this,
                    std::placeholders::_1));

    app_->register_availability_handler(vsomeip::ANY_SERVICE,
            vsomeip::ANY_INSTANCE,
            std::bind(&payload_test_client::on_availability, this,
                    std::placeholders::_1, std::placeholders::_2,
                    std::placeholders::_3));
    config_service_info();
    return true;
}

void payload_test_client::start()
{
    VSOMEIP_INFO << "Starting...";
    app_->start();
}

void payload_test_client::signal_stop()
{
    VSOMEIP_INFO << "Stopping by signal...";
    // shutdown the service
    blocked_ = true;
    running_ = false;
    condition_.notify_one();
    wait_for_stop_ =true;
    wait_for_stop_cv_.notify_one();
    do_not_wait.store(true);
}

void payload_test_client::stop()
{
    VSOMEIP_INFO << "Stopping...";
    
    for(const struct service_info& i: service_infos) {
        if (i.service_id == 0xFFFF && i.instance_id == 0xFFFF) 
        {
            continue;
        }
        app_->release_service(i.service_id, i.instance_id);
    }

    app_->release_service(vsomeip_test::TEST_SERVICE_SERVICE_ID, vsomeip_test::TEST_SERVICE_INSTANCE_ID);
    app_->clear_all_handler();
}

void payload_test_client::shutdown_service()
{
    request_->set_service(vsomeip_test::TEST_SERVICE_SERVICE_ID);
    request_->set_instance(vsomeip_test::TEST_SERVICE_INSTANCE_ID);
    request_->set_method(vsomeip_test::TEST_SERVICE_METHOD_ID_SHUTDOWN);
    
    app_->send(request_);
    for(const struct service_info& i: service_infos) {
        if (i.service_id == 0xFFFF && i.instance_id == 0xFFFF) 
        {
            continue;
        }
        request_->set_service(i.service_id);
        request_->set_instance(i.instance_id);
        request_->set_method(vsomeip_test::TEST_SERVICE_METHOD_ID_SHUTDOWN);
        app_->send(request_);
    }
}

void payload_test_client::join_sender_thread()
{
    sender_.join();
}

void payload_test_client::on_state(vsomeip::state_type_e _state)
{
    if(_state == vsomeip::state_type_e::ST_REGISTERED)
    {
        app_->request_service(vsomeip_test::TEST_SERVICE_SERVICE_ID,
                vsomeip_test::TEST_SERVICE_INSTANCE_ID, false);

        for(const struct service_info& i: service_infos) {
            if (i.service_id == 0xFFFF && i.instance_id == 0xFFFF) 
            {
                continue;
            }
            app_->request_service(i.service_id, i.instance_id, false);
        }
    }
    if (!app_->is_routing())
        app_->request_service(0x8888, 0x8888, false);
}

void payload_test_client::on_availability(vsomeip::service_t _service,
        vsomeip::instance_t _instance, bool _is_available)
{
    VSOMEIP_INFO << "Service [" << std::hex
            << _service << "." << _instance << "] is "
            << (_is_available ? "available." : "NOT available.");

    if(vsomeip_test::TEST_SERVICE_SERVICE_ID == _service
            && vsomeip_test::TEST_SERVICE_INSTANCE_ID == _instance)
    {
        if(is_available_ && !_is_available)
        {
            is_available_ = false;
        }
        else if(_is_available && !is_available_)
        {
            is_available_ = true;
            services_available_[std::make_pair(vsomeip_test::TEST_SERVICE_SERVICE_ID, vsomeip_test::TEST_SERVICE_INSTANCE_ID)] = true;
            //send();
        }
    }
    else if(0x8888 == _service && !app_->is_routing())
    {
        VSOMEIP_INFO << "[" << "] Service ["
            <<  std::hex << _service << "." << _instance
            << "] is available.";
        request_->set_service(0x8888);
        request_->set_instance(0x8888);
        request_->set_method(0x8888);
        app_->send(request_);
    }
    else
    {
        auto its_service = services_available_.find(std::make_pair(_service, _instance));
        if(its_service != services_available_.end()) {
            if(its_service->second != _is_available) {
            its_service->second = true;
            VSOMEIP_INFO << "[" << "] Service ["
            <<  std::hex << _service << "." << _instance
            << "] is available.";

            }
        }
    }

    if(std::all_of(services_available_.cbegin(),
                           services_available_.cend(),
                           [](const std::map<std::pair<vsomeip::service_t,
                                   vsomeip::instance_t>, bool>::value_type& v) {
                                return v.second;})) {
                send();
            }

}

void payload_test_client::on_message(const std::shared_ptr<vsomeip::message>& _response)
{
    if(_response->get_service() == 0x8888)
    {
        other_client_exist = true;
        return;
    }
    number_of_acknowledged_messages_++;

    if(call_service_sync_)
    {
        // We notify the sender thread every time a message was acknowledged
        {
            std::lock_guard<std::mutex> lk(all_msg_acknowledged_mutex_);
            all_msg_acknowledged_ = true;
        }
        all_msg_acknowledged_cv_.notify_one();
    }
    else
    {
        // We notify the sender thread only if all sent messages have been acknowledged
        if(number_of_acknowledged_messages_ == number_of_messages_to_send_)
        {
            std::lock_guard<std::mutex> lk(all_msg_acknowledged_mutex_);
            number_of_acknowledged_messages_ = 0;
            all_msg_acknowledged_ = true;
            all_msg_acknowledged_cv_.notify_one();
        }
        else if(number_of_acknowledged_messages_ % sliding_window_size_ == 0)
        {
            std::lock_guard<std::mutex> lk(all_msg_acknowledged_mutex_);
            all_msg_acknowledged_ = true;
            all_msg_acknowledged_cv_.notify_one();
        }
    }
}

void payload_test_client::send()
{
    std::lock_guard<std::mutex> its_lock(mutex_);
    blocked_ = true;
    condition_.notify_one();
}

void payload_test_client::wait_for_stop()
{
    std::unique_lock<std::mutex> wk(wait_for_stop_mutex_);
    while (!wait_for_stop_)
    {
        wait_for_stop_cv_.wait(wk);
    }
    // shutdown the service
    if(shutdown_service_at_end && !other_client_exist)
    {
        shutdown_service();
    }
    if (app_->is_routing()) {
        VSOMEIP_INFO<< "[ Payload Test ] : : client ["
            << std::hex << app_->get_client() << "] is routing manager";
        for (const auto& i : service_infos) {
            if (i.service_id == 0xFFFF && i.instance_id == 0xFFFF) {
                continue;
            }
            while (app_->is_available(i.service_id, i.instance_id,
                    vsomeip::ANY_MAJOR, vsomeip::ANY_MINOR) && !do_not_wait.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
    stop();
    std::thread t1([](){ std::this_thread::sleep_for(std::chrono::microseconds(1000000 * 5));});
    t1.join();
    app_->stop();
    std::thread t([](){ std::this_thread::sleep_for(std::chrono::microseconds(1000000 * 5));});
    t.join();
}

void payload_test_client::run()
{
    std::uint32_t   index = 1;
    std::unique_lock<std::mutex> its_lock(mutex_);
    running_ = true;
    while (!blocked_)
    {
        condition_.wait(its_lock);
    }
    VSOMEIP_INFO << "start run";
    stop_thread_  = std::thread(std::bind(&payload_test_client::wait_for_stop, this));

    if(app_->is_routing())
        app_->offer_service(0x8888, 0x8888);

    request_->set_service(vsomeip_test::TEST_SERVICE_SERVICE_ID);
    request_->set_instance(vsomeip_test::TEST_SERVICE_INSTANCE_ID);
    request_->set_method(vsomeip_test::TEST_SERVICE_METHOD_ID);

    // lock the mutex
    std::unique_lock<std::mutex> lk(all_msg_acknowledged_mutex_);

    std::uint32_t max_allowed_payload = get_max_allowed_payload();

    std::shared_ptr<vsomeip::payload> payload = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> payload_data;
    bool reached_peak = false;

    for(;running_;)
    {
        payload_data.assign(current_payload_size_ , vsomeip_test::PAYLOAD_TEST_DATA);
        payload->set_data(payload_data);
        request_->set_payload(payload);
        
        request_->set_service(service_infos[index].service_id);
        request_->set_instance(service_infos[index].instance_id);
        request_->set_method(service_infos[index].method_id);
        services_messages_sent[std::make_pair(service_infos[index].service_id, service_infos[index].instance_id)]++;

        index = (++index % service_infos.size()) ? index % service_infos.size() : 1;

        call_service_sync_ ? send_messages_sync(lk) : send_messages_async(lk);
        if(number_of_sent_messages_ < number_of_messages_to_send_)
            continue;

        for(const struct service_info& i: service_infos) {
            if (i.service_id == 0xFFFF && i.instance_id == 0xFFFF) 
            {
                continue;
            }
            VSOMEIP_INFO<< "[ Payload Test ] : : client ["
            << std::hex << app_->get_client() << "] sent to service/instance ["
            << std::hex << i.service_id << "." << std::hex << i.instance_id << "] " 
            << std::dec << services_messages_sent[std::make_pair(i.service_id, i.instance_id)]
            << " messages payload size " << current_payload_size_;
            services_messages_sent[std::make_pair(i.service_id, i.instance_id)] = 0;
        }

        number_of_sent_messages_ = 0;
        // Increase array size for next iteration
        if(!reached_peak) {
            current_payload_size_ *= 2;
        } else {
            current_payload_size_ /= 2;
        }

        if(!reached_peak && current_payload_size_ > max_allowed_payload)
        {
            current_payload_size_ = max_allowed_payload;
            reached_peak = true;
        } else if(reached_peak && current_payload_size_ <= 1) {
            break;
        }
    }
    blocked_ = false;
    VSOMEIP_INFO<< "[ Payload Test ] : : client ["
            << std::hex << app_->get_client() << "] TOTAL MESSAGES SENT "
            << std::dec << number_of_sent_messages_total_;
    std::lock_guard<std::mutex> wk(wait_for_stop_mutex_);
    wait_for_stop_ = true;
    wait_for_stop_cv_.notify_one();
}

std::uint32_t payload_test_client::get_max_allowed_payload()
{
    std::uint32_t payload;
    switch (max_payload_size)
    {
        case payloadsize::UDS:
            // TODO
            payload = VSOMEIP_MAX_UDP_MESSAGE_SIZE - 16;//1024 * 32 - 16;
            break;
        case payloadsize::TCP:
            // TODO
            payload = 4095 - 16;
            break;
        case payloadsize::UDP:
            payload = VSOMEIP_MAX_UDP_MESSAGE_SIZE - 16;
            break;
        case payloadsize::USER_SPECIFIED:
            payload = user_defined_max_payload;
            break;
        default:
            payload = VSOMEIP_MAX_LOCAL_MESSAGE_SIZE;
            break;
    }
    return payload;
}

void payload_test_client::send_messages_sync(std::unique_lock<std::mutex>& lk)
{
    app_->send(request_);
    number_of_sent_messages_++;
    number_of_sent_messages_total_++;
    all_msg_acknowledged_cv_.wait(lk, [&]
        {   return all_msg_acknowledged_;});
    all_msg_acknowledged_ = false;
}

void payload_test_client::send_messages_async(std::unique_lock<std::mutex>& lk)
{
    app_->send(request_);
    number_of_sent_messages_++;
    number_of_sent_messages_total_++;
    if((number_of_sent_messages_+1) % sliding_window_size_ == 0)
    {
        all_msg_acknowledged_cv_.wait(lk, [&]
            {   return all_msg_acknowledged_;});
        all_msg_acknowledged_ = false;
    }
}

#ifndef _WIN32
int main(int argc, char** argv)
{
    std::string tcp_enable("--tcp");
    std::string udp_enable("--udp");
    std::string sync_enable("--sync");
    std::string async_enable("--async");
    std::string sliding_window_size_param("--sliding-window-size");
    std::string max_payload_size_param("--max-payload-size");
    std::string shutdown_service_disable_param("--dont-shutdown-service");
    std::string numbers_of_messages("--number-of-messages");
    std::string help("--help");

    int i = 1;
    while (i < argc)
    {
        if(tcp_enable == argv[i])
        {
            use_tcp = true;
        }
        else if(udp_enable == argv[i])
        {
            use_tcp = false;
        }
        else if(sync_enable == argv[i])
        {
            call_service_sync = true;
        }
        else if(async_enable == argv[i])
        {
            call_service_sync = false;
        }
        else if(sliding_window_size_param == argv[i] && i + 1 < argc)
        {
            i++;
            std::stringstream converter(argv[i]);
            converter >> sliding_window_size;
        }
        else if(max_payload_size_param == argv[i] && i + 1 < argc)
        {
            i++;
            if(std::string("UDS") == argv[i])
            {
                max_payload_size = payloadsize::UDS;
            }
            else if(std::string("TCP") == argv[i])
            {
                max_payload_size = payloadsize::TCP;
            }
            else if(std::string("UDP") == argv[i])
            {
                max_payload_size = payloadsize::UDP;
            }
            else {
                max_payload_size = payloadsize::USER_SPECIFIED;
                std::stringstream converter(argv[i]);
                converter >> user_defined_max_payload;
            }
        }
        else if (numbers_of_messages == argv[i]) {
            i++;
            std::stringstream converter(argv[i]);
            converter >> number_of_messages_to_send;
        }
        else if(shutdown_service_disable_param == argv[i])
        {
            shutdown_service_at_end = false;
        }
        else if(help == argv[i])
        {
            VSOMEIP_INFO << "Parameters:\n"
            << "--tcp: Send messages via TCP\n"
            << "--udp: Send messages via UDP (default)\n"
            << "--sync: Wait for acknowledge before sending next message (default)\n"
            << "--async: Send multiple messages w/o waiting for"
                " acknowledge of service\n"
            << "--sliding-window-size: Number of messages to send before waiting "
                "for acknowledge of service. Default: " << sliding_window_size << "\n"
            << "--max-payload-size: limit the maximum payloadsize of send requests. One of {"
                "UDS (=" << VSOMEIP_MAX_LOCAL_MESSAGE_SIZE << "byte), "
                "UDP (=" << VSOMEIP_MAX_UDP_MESSAGE_SIZE << "byte), "
                "TCP (=" << VSOMEIP_MAX_TCP_MESSAGE_SIZE << "byte)}, default: UDS\n"
            << "--dont-shutdown-service: Don't shutdown the service upon "
                "finishing of the payload test\n"
            << "--number-of-messages: Number of messages to send per payload size iteration\n"
            << "--help: print this help";
        }
        i++;
    }

    payload_test_client test_client_(use_tcp, call_service_sync, sliding_window_size);
    #ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
    its_sample_ptr = &test_client_;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    #endif
    if (test_client_.init()) {
        test_client_.start();
        test_client_.join_sender_thread();
    }

    VSOMEIP_INFO << "Client process end\n";
}
#endif
