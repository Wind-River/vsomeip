//
// Copyright (c) 2023 Wind River Systems, Inc.
//
// Copyright (C) 2015-2017 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "payload_test_service.hpp"
#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
#include <csignal>
#endif
// this variables are changed via cmdline parameters

static bool check_payload = true;
static unsigned long service_number;
struct service_info {
    vsomeip::service_t service_id;
    vsomeip::instance_t instance_id;
    vsomeip::method_t method_id;
    };
static std::array<service_info, 4> service_infos = {{
    }};
static constexpr std::array<service_info, 4> service_infos_1 = {{
            { 0xFFFF, 0xFFFF, 0xFFFF },
            { 0x1000, 0x1111, 0x1111 },
            { 0x2000, 0x1111, 0x2222 },
            { 0x3000, 0x2222, 0x3333 }
    }};
static constexpr std::array<service_info, 4> service_infos_2 = {{
            { 0xFFFF, 0xFFFF, 0xFFFF },
            { 0x4000, 0x4444, 0x4 },
            { 0x5000, 0x5555, 0x5 },
            { 0x6000, 0x6666, 0x6 }
    }};
payload_test_service::payload_test_service() :
                app_(vsomeip::runtime::get()->create_application()),
                is_registered_(false),
                blocked_(false),
                number_of_received_messages_(0),
                number_of_recv_messages_total_(0),
                offer_thread_(std::bind(&payload_test_service::run, this))
{
}
#ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
    payload_test_service *its_sample_ptr(nullptr);
    void handle_signal(int _signal) {
        if (its_sample_ptr != nullptr &&
                (_signal == SIGINT || _signal == SIGTERM)) {
                    its_sample_ptr->stop();
                }
    }
#endif
bool payload_test_service::init()
{
    std::lock_guard<std::mutex> its_lock(mutex_);

    if (!app_->init()) {
        return false;
    }

    app_->register_message_handler(vsomeip::ANY_SERVICE, vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD,
            std::bind(&payload_test_service::on_message, this,
                    std::placeholders::_1));

    app_->register_message_handler(vsomeip::ANY_SERVICE,
            vsomeip::ANY_INSTANCE,
            vsomeip_test::TEST_SERVICE_METHOD_ID_SHUTDOWN,
            std::bind(&payload_test_service::on_message_shutdown, this,
                    std::placeholders::_1));

    app_->register_state_handler(
            std::bind(&payload_test_service::on_state, this,
                    std::placeholders::_1));
    return true;
}

void payload_test_service::print_all_messages(vsomeip::client_t c, std::uint32_t payload_size)
{
    auto iter = services_messages_recived.begin();
    while (iter != services_messages_recived.end())
    {
        if((c == iter->first.first || vsomeip::ANY_CLIENT == c) &&
            payload_size == iter->first.second.second && iter->second != 0) {
            VSOMEIP_INFO << "Received " << std::dec << iter->second 
                << " message with Client/Service/Instance [" 
                << std::hex << iter->first.first << "."
                << std::hex << iter->first.second.first.first << "."
                << std::hex << iter->first.second.first.second << "] payload size [byte]:"
                << std::dec << iter->first.second.second;
                number_of_recv_messages_total_ += iter->second;
                iter->second = 0;
        }
        iter++;
    }
    
}
void payload_test_service::start()
{
    VSOMEIP_INFO << "Starting...";
    app_->start();
}

void payload_test_service::stop()
{
    VSOMEIP_INFO << "Stopping...";
    blocked_ = true;
    print_all_messages(vsomeip::ANY_CLIENT, 2);
    condition_.notify_one();
    app_->clear_all_handler();
    VSOMEIP_INFO<< "client ["
            << std::hex << app_->get_client() << "] TOTAL MESSAGES RECV "
            << std::dec << number_of_recv_messages_total_;
    app_->stop();
}

void payload_test_service::join_offer_thread()
{
    offer_thread_.join();
}

void payload_test_service::offer()
{
    if(service_number == 2)
        app_->offer_service(vsomeip_test::TEST_SERVICE_SERVICE_ID, vsomeip_test::TEST_SERVICE_INSTANCE_ID);
    for(const struct service_info& i: service_infos) {
        if (i.service_id == 0xFFFF && i.instance_id == 0xFFFF) 
        {
            continue;
        }
        app_->offer_service(i.service_id, i.instance_id);
    }
}

void payload_test_service::stop_offer()
{
    if(service_number == 1)
        app_->stop_offer_service(vsomeip_test::TEST_SERVICE_SERVICE_ID, vsomeip_test::TEST_SERVICE_INSTANCE_ID);
    for(const struct service_info& i: service_infos) {
        if (i.service_id == 0xFFFF && i.instance_id == 0xFFFF) 
        {
            continue;
        }
        app_->stop_offer_service(i.service_id, i.instance_id);
    }
}

void payload_test_service::on_state(vsomeip::state_type_e _state)
{
    VSOMEIP_INFO << "Application " << app_->get_name() << " is "
            << (_state == vsomeip::state_type_e::ST_REGISTERED ? "registered." :
                    "deregistered.");

    if(_state == vsomeip::state_type_e::ST_REGISTERED)
    {
        if(!is_registered_)
        {
            is_registered_ = true;
            std::lock_guard<std::mutex> its_lock(mutex_);
            blocked_ = true;
            // "start" the run method thread
            condition_.notify_one();
        }
    }
    else
    {
        is_registered_ = false;
    }
}

void payload_test_service::on_message(const std::shared_ptr<vsomeip::message>& _request)
{
    number_of_received_messages_++;
    if(last_payload_size.find(std::make_pair(_request->get_client(), _request->get_service())) != last_payload_size.end() &&
        last_payload_size[std::make_pair(_request->get_client(), _request->get_service())] != _request->get_payload()->get_length()) {
        print_all_messages(_request->get_client(), last_payload_size[std::make_pair(_request->get_client(), _request->get_service())]);
    }
    last_payload_size[std::make_pair(_request->get_client(), _request->get_service())] = _request->get_payload()->get_length();
    services_messages_recived[std::make_pair(_request->get_client(), 
        std::make_pair(
            std::make_pair(_request->get_service(), _request->get_instance()), _request->get_payload()->get_length())
        )]++;

    if (check_payload) {
        std::shared_ptr<vsomeip::payload> pl = _request->get_payload();
        vsomeip::byte_t* pl_ptr = pl->get_data();
        for (vsomeip::length_t i = 0; i < pl->get_length(); i++)
        {
        }
    }

    // send response
    std::shared_ptr<vsomeip::message> its_response =
            vsomeip::runtime::get()->create_response(_request);

    app_->send(its_response);
}

void payload_test_service::on_message_shutdown(
        const std::shared_ptr<vsomeip::message>& _request)
{
    (void)_request;
    VSOMEIP_INFO << "Shutdown method was called, going down now.";
    stop();
}

void payload_test_service::run()
{
    std::unique_lock<std::mutex> its_lock(mutex_);
    while (!blocked_)
        condition_.wait(its_lock);

   offer();
}

#ifndef _WIN32
int main(int argc, char** argv)
{
    std::string help("--help");
    std::string check("--do-not-check-payload");

    service_infos = service_infos_1;
    service_number = std::stoul(std::string(argv[1]), nullptr);
    if(service_number == 2)
        service_infos = service_infos_2;

    int i = 2;
    while (i < argc)
    {
        if(help == argv[i])
        {
            VSOMEIP_INFO << "Parameters:\n"
                    << "--help: print this help\n"
                    << "--do-not-check-payload: Don't verify payload data "
                    << "-> Use this flag for performance measurements!";
        } else if (check == argv[i]) {
            check_payload = false;
        }
        i++;
    }
    payload_test_service test_service;
    #ifndef VSOMEIP_ENABLE_SIGNAL_HANDLING
    its_sample_ptr = &test_service;
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    #endif
    if (test_service.init()) {
        test_service.start();
        test_service.join_offer_thread();
    }
}
#endif
