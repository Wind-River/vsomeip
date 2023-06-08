//
// Copyright (c) 2023 Wind River Systems, Inc.
//
// Copyright (C) 2014-2017 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <map>
#include <algorithm>
#include <atomic>

#include <vsomeip/vsomeip.hpp>
#include <vsomeip/internal/logger.hpp>

#include "subscribe_notify_test_globals.hpp"

static bool endless = false;
class subscribe_notify_test_service {
public:
    subscribe_notify_test_service(struct subscribe_notify_test::service_info _service_info,
                                  std::array<subscribe_notify_test::service_info, 7> _service_infos,
                                  vsomeip::reliability_type_e _reliability_type) :
            service_info_(_service_info),
            service_infos_(_service_infos),
            app_(vsomeip::runtime::get()->create_application()),
            wait_until_registered_(true),
            wait_until_other_services_available_(true),
            wait_until_notified_from_other_services_(true),
            offer_thread_(std::bind(&subscribe_notify_test_service::run, this)),
            wait_for_stop_(true),
            stop_thread_(std::bind(&subscribe_notify_test_service::wait_for_stop, this)),
            wait_for_notify_(true),
            notify_thread_(std::bind(&subscribe_notify_test_service::notify, this)),
            subscription_state_handler_called_(0),
            subscription_error_occured_(false),
            reliability_type_(_reliability_type) {
        if (!app_->init()) {
            return;
        }

        app_->register_state_handler(
                std::bind(&subscribe_notify_test_service::on_state, this,
                        std::placeholders::_1));
        app_->register_message_handler(service_info_.service_id,
                service_info_.instance_id, service_info_.method_id,
                std::bind(&subscribe_notify_test_service::on_request, this,
                        std::placeholders::_1));
        app_->register_message_handler(vsomeip::ANY_SERVICE,
                vsomeip::ANY_INSTANCE, vsomeip::ANY_METHOD,
                std::bind(&subscribe_notify_test_service::on_message, this,
                        std::placeholders::_1));

        // offer event
        std::set<vsomeip::eventgroup_t> its_eventgroups;
        its_eventgroups.insert(service_info_.eventgroup_id);
        app_->offer_event(service_info_.service_id, service_info_.instance_id,
                service_info_.event_id, its_eventgroups,
                vsomeip::event_type_e::ET_FIELD, std::chrono::milliseconds::zero(),
                false, true, nullptr, reliability_type_);


        // register availability for all other services and request their event.
        for(const auto& i : service_infos_) {
            if ((i.service_id == service_info_.service_id
                    && i.instance_id == service_info_.instance_id)
                    || (i.service_id == 0xFFFF && i.instance_id == 0xFFFF)) {
                continue;
            }
            app_->request_service(i.service_id, i.instance_id);
            app_->register_availability_handler(i.service_id, i.instance_id,
                    std::bind(&subscribe_notify_test_service::on_availability, this,
                            std::placeholders::_1, std::placeholders::_2,
                            std::placeholders::_3));

            auto handler = std::bind(&subscribe_notify_test_service::on_subscription_state_change, this,
                    std::placeholders::_1, std::placeholders::_2,
                    std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
            app_->register_subscription_status_handler(i.service_id, i.instance_id, i.eventgroup_id, vsomeip::ANY_EVENT, handler);


            std::set<vsomeip::eventgroup_t> its_eventgroups;
            its_eventgroups.insert(i.eventgroup_id);
            app_->request_event(i.service_id, i.instance_id, i.event_id, its_eventgroups, vsomeip::event_type_e::ET_FIELD, reliability_type_);

            other_services_available_[std::make_pair(i.service_id, i.instance_id)] = false;
            other_services_received_notification_[std::make_pair(i.service_id, i.method_id)] = 0;
            other_services_received_request_[std::make_pair(i.service_id, i.method_id)] = 0;
            other_services_received_response_[std::make_pair(i.service_id, i.method_id)] = 0;
        }

        // register subscription handler to detect whether or not all other
        // other services have subscribed
        app_->register_subscription_handler(service_info_.service_id,
                service_info_.instance_id, service_info_.eventgroup_id,
                std::bind(&subscribe_notify_test_service::on_subscription, this,
                        std::placeholders::_1, std::placeholders::_2,
                        std::placeholders::_3, std::placeholders::_4));
        app_->start();
    }

    ~subscribe_notify_test_service() {
        offer_thread_.join();
        stop_thread_.join();
    }

    void offer() {
        app_->offer_service(service_info_.service_id, service_info_.instance_id);
    }

    void stop_offer() {
        app_->stop_offer_event(service_info_.service_id, service_info_.instance_id, service_info_.event_id);
        app_->stop_offer_service(service_info_.service_id, service_info_.instance_id);
    }

    void signal_stop()
    {
        VSOMEIP_INFO << "Stopping by signal...";
        // shutdown the service
        endless = false;
        std::lock_guard<std::mutex> notify_lock(notify_mutex_);
        wait_for_notify_ = false;
        notify_condition_.notify_one();

        std::lock_guard<std::mutex> its_lock(mutex_);
        wait_until_notified_from_other_services_ = false;
        condition_.notify_one();

        std::lock_guard<std::mutex> stop_lock(stop_mutex_);
        wait_for_stop_ = false;
        stop_condition_.notify_one();
    }
    void on_state(vsomeip::state_type_e _state) {
        VSOMEIP_INFO << "Application " << app_->get_name() << " is "
        << (_state == vsomeip::state_type_e::ST_REGISTERED ?
                "registered." : "deregistered.");

        if (_state == vsomeip::state_type_e::ST_REGISTERED) {
            std::lock_guard<std::mutex> its_lock(mutex_);
            wait_until_registered_ = false;
            condition_.notify_one();
        }
    }

    void on_availability(vsomeip::service_t _service,
                         vsomeip::instance_t _instance, bool _is_available) {
        if(_is_available) {
            auto its_service = other_services_available_.find(std::make_pair(_service, _instance));
            if(its_service != other_services_available_.end()) {
                if(its_service->second != _is_available) {
                its_service->second = true;
                VSOMEIP_INFO << "[" << std::setw(4) << std::setfill('0') << std::hex
                        << service_info_.service_id << "] Service ["
                << std::setw(4) << std::setfill('0') << std::hex << _service << "." << _instance
                << "] is available.";

                }
            }

            if(std::all_of(other_services_available_.cbegin(),
                           other_services_available_.cend(),
                           [](const std::map<std::pair<vsomeip::service_t,
                                   vsomeip::instance_t>, bool>::value_type& v) {
                                return v.second;})) {
                std::lock_guard<std::mutex> its_lock(mutex_);
                wait_until_other_services_available_ = false;
                condition_.notify_one();
            }
        }
    }

    void on_subscription_state_change(const vsomeip::service_t _service, const vsomeip::instance_t _instance,
            const vsomeip::eventgroup_t _eventgroup, const vsomeip::event_t _event, const uint16_t _error) {
        (void)_service;
        (void)_instance;
        (void)_eventgroup;
        (void)_event;
        
        if (!_error) {
            subscription_state_handler_called_++;
        } else {
            subscription_error_occured_ = true;
            VSOMEIP_WARNING << std::hex << app_->get_client()
                    << " : on_subscription_state_change: for service " << std::hex
                    << _service << " received a subscription error!";
        }
   }

    bool on_subscription(vsomeip::client_t _client, std::uint32_t _uid, std::uint32_t _gid, bool _subscribed) {
        (void)_uid;
        (void)_gid;
        std::lock_guard<std::mutex> its_lock(subscribers_mutex_);
        static bool notified(false);
        if (_subscribed) {
            subscribers_.insert(_client);
        } else {
            subscribers_.erase(_client);
            if(subscribers_.size() == 0)
            {
            }
        }

        VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                << service_info_.service_id << "] " << "Client: "
                << std::setw(4) << std::setfill('0') << std::hex << _client
                << (_subscribed ? " subscribed" : " unsubscribed")
                << ", now have " << std::dec << subscribers_.size()
                << " subscribers" ;
        // check if all other services have subscribed:
        // -1 for placeholder in array
        // divide by two because we only receive once subscription per remote node
        // no matter how many clients subscribed to this eventgroup on the remote node
        if (subscribers_.size() == (service_infos_.size() - 1) / 2 )
        {
            // notify the notify thread to start sending out notifications
            std::lock_guard<std::mutex> its_lock(notify_mutex_);
            wait_for_notify_ = false;
            notify_condition_.notify_one();
            notified = true;
        }
        return true;
    }

    void on_request(const std::shared_ptr<vsomeip::message> &_message) {
        if(_message->get_message_type() == vsomeip::message_type_e::MT_REQUEST) {
            VSOMEIP_DEBUG << "Received a request with Client/Session [" << std::setw(4)
            << std::setfill('0') << std::hex << _message->get_client() << "/"
            << std::setw(4) << std::setfill('0') << std::hex
            << _message->get_session() << "]";
            std::shared_ptr<vsomeip::message> its_response = vsomeip::runtime::get()
            ->create_response(_message);
            app_->send(its_response);
        }
    }

    void on_message(const std::shared_ptr<vsomeip::message> &_message) {
        if(_message->get_message_type() == vsomeip::message_type_e::MT_NOTIFICATION) {

            other_services_received_notification_[std::make_pair(_message->get_service(),  _message->get_method())]++;
            if(endless == false || other_services_received_notification_[std::make_pair(_message->get_service(),
                _message->get_method())] % subscribe_notify_test::notifications_to_send == 0)
                VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                << service_info_.service_id << "] "
                << "Received a notification with Client/Session [" << std::setw(4)
                << std::setfill('0') << std::hex << _message->get_client() << "/"
                << std::setw(4) << std::setfill('0') << std::hex
                << _message->get_session() << "] from Service/Method ["
                << std::setw(4) << std::setfill('0') << std::hex
                << _message->get_service() << "/" << std::setw(4) << std::setfill('0')
                << std::hex << _message->get_method() << "/" << std::dec << _message->get_length() << "] (now have: "
                << std::dec << other_services_received_notification_[std::make_pair(_message->get_service(),  _message->get_method())] << ")";
            if(other_services_received_notification_[std::make_pair(_message->get_service(),
                _message->get_method())] % 2 == 0)
            {
                std::shared_ptr<vsomeip::message> request_ = vsomeip::runtime::get()->create_request(false);
                std::shared_ptr<vsomeip::payload> payload = vsomeip::runtime::get()->create_payload();
                std::vector<vsomeip::byte_t> payload_data;
                std::ostringstream req_str;
                req_str << "client [" << std::hex << app_->get_client() << "] recive "
                    << "notify from client/service/method [" << std::hex << _message->get_client()
                    << "." << std::hex << _message->get_service() 
                    << "." << std::hex << _message->get_method() << "]";
                std::string str = req_str.str();
                payload_data.assign(str.begin() , str.end());
                payload->set_data(payload_data);
                request_->set_payload(payload);

                request_->set_service(_message->get_service());
                request_->set_instance(_message->get_instance());
                request_->set_method(_message->get_method());
                app_->send(request_);
                other_services_received_request_[std::make_pair(_message->get_service(), _message->get_method())]++;
                VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex << service_info_.service_id << "] "
                << "Send a request to Client/Session [" << std::setw(4)
                << std::setfill('0') << std::hex << _message->get_client() << "/"
                << std::setw(4) << std::setfill('0') << std::hex
                << _message->get_session() << "] with Service/Method ["
                << std::setw(4) << std::setfill('0') << std::hex
                << _message->get_service() << "/" << std::setw(4) << std::setfill('0')
                << std::hex << _message->get_method() << "/" << std::dec << request_->get_length() << "] (now sent: "
                << std::dec << other_services_received_request_[std::make_pair(_message->get_service(), _message->get_method())] << ")";
            }
            if(all_notifications_received()) {
                std::lock_guard<std::mutex> its_lock(mutex_);
                wait_until_notified_from_other_services_ = false;
                condition_.notify_one();
                for(const auto& i : service_infos_) {
                if ((i.service_id == service_info_.service_id
                        && i.instance_id == service_info_.instance_id)
                        || (i.service_id == 0xFFFF && i.instance_id == 0xFFFF)) {
                    continue;
                }
                other_services_received_notification_[std::make_pair(i.service_id, i.method_id)] = 0;
                }
            }
        }
        else if(_message->get_message_type() == vsomeip::message_type_e::MT_REQUEST)
        {
            if(_message->get_method() == 0x8888){
                sub_services_.insert(_message->get_client());
                if(sub_services_.size() == 5) {
                    std::lock_guard<std::mutex> sl(wait_unsubscribe_mutex_);
                    wait_until_unsubscribe_ = false;
                    wait_unsubscribe_condition_.notify_one();
                    sub_services_.clear();
                }
                return;
            }
            std::shared_ptr<vsomeip::message> its_response =
            vsomeip::runtime::get()->create_response(_message);
            other_services_received_response_[std::make_pair(_message->get_service(),
                                                                    _message->get_method())]++;
            VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
            << service_info_.service_id << "] "
            << "Received a request with Client/Session [" << std::setw(4)
            << std::setfill('0') << std::hex << _message->get_client() << "/"
            << std::setw(4) << std::setfill('0') << std::hex
            << _message->get_session() << "] from Service/Method ["
            << std::setw(4) << std::setfill('0') << std::hex
            << _message->get_service() << "/" << std::setw(4) << std::setfill('0')
            << std::hex << _message->get_method() << "/" << std::dec << _message->get_length() << "] (now have: "
            << std::dec << other_services_received_response_[std::make_pair(_message->get_service(), _message->get_method())] << ")";

            app_->send(its_response);
            
        }
    }

    bool all_notifications_received() {
        return std::all_of(
                other_services_received_notification_.cbegin(),
                other_services_received_notification_.cend(),
                [&](const std::map<std::pair<vsomeip::service_t,
                        vsomeip::method_t>, std::uint32_t>::value_type& v)
                {
                    return v.second >= subscribe_notify_test::notifications_to_send;
                }
        );
    }

    bool all_notifications_received_tcp_and_udp() {
        std::uint32_t received_twice(0);
        std::uint32_t received_normal(0);
        for(const auto &v : other_services_received_notification_) {
            if (v.second == subscribe_notify_test::notifications_to_send * 2) {
                received_twice++;
            } else if(v.second == subscribe_notify_test::notifications_to_send) {
                received_normal++;
            }
        }

        if(   received_twice == (service_infos_.size() - 1) / 2
           && received_normal == (service_infos_.size() - 1) / 2 - 1) {
            // routing manager stub receives the notification
            // - twice from external nodes
            // - and normal from all internal nodes
            VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                        << service_info_.service_id << "] "
                        << "Received notifications:"
                        << " Normal: " << received_normal
                        << " Twice: " << received_twice;
            return true;
        }
        return false;
    }

    void run() {
        VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                << service_info_.service_id << "] Running";
        std::unique_lock<std::mutex> its_lock(mutex_);
        while (wait_until_registered_) {
            condition_.wait(its_lock);
        }
        wait_until_unsubscribe_ = true;
        VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                << service_info_.service_id << "] Offering";
        offer();


        while (wait_until_other_services_available_) {
            condition_.wait(its_lock);
        }
        do {
            VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                    << service_info_.service_id << "] Subscribing";
            // subscribe to events of other services
            uint32_t subscribe_count = 0;
            for(const subscribe_notify_test::service_info& i: service_infos_) {
                if ((i.service_id == service_info_.service_id
                                && i.instance_id == service_info_.instance_id)
                        || (i.service_id == 0xFFFF && i.instance_id == 0xFFFF)) {
                    continue;
                }
                ++subscribe_count;
                app_->subscribe(i.service_id, i.instance_id, i.eventgroup_id,
                                vsomeip::DEFAULT_MAJOR);
                other_services_received_notification_[std::make_pair(i.service_id, i.method_id)] = 0;
                VSOMEIP_DEBUG << "[" << std::hex << service_info_.service_id
                << "] subscribing to Service/Instance/Eventgroup ["
                << std::setw(4) << std::setfill('0') << std::hex << i.service_id << "/"
                << std::setw(4) << std::setfill('0') << std::hex << i.instance_id
                << "/" << std::setw(4) << std::setfill('0') << std::hex << i.eventgroup_id <<"]";
            }
            while (wait_until_notified_from_other_services_) {
                condition_.wait(its_lock);
            }
            wait_until_notified_from_other_services_ = true;
            for(const auto& i : service_infos_) {
                if ((i.service_id == service_info_.service_id
                        && i.instance_id == service_info_.instance_id)
                        || (i.service_id == 0xFFFF && i.instance_id == 0xFFFF)) {
                    continue;
                }

                std::shared_ptr<vsomeip::message> request_;
                request_ = vsomeip::runtime::get()->create_request(false);
                request_->set_service(i.service_id);
                request_->set_instance(i.instance_id);
                request_->set_method(0x8888);
                app_->send(request_);
                other_services_received_notification_[std::make_pair(i.service_id, i.method_id)] = 0;
                app_->unsubscribe(i.service_id, i.instance_id, i.eventgroup_id);
            }
            std::unique_lock<std::mutex> sl(wait_unsubscribe_mutex_);
            while (endless && wait_until_unsubscribe_)
            {
                wait_unsubscribe_condition_.wait(sl);
            }
            wait_until_unsubscribe_ = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        while(endless);

        std::lock_guard<std::mutex> stop_lock(stop_mutex_);
        wait_for_stop_ = false;
        stop_condition_.notify_one();

        // It is possible that we run in the case a subscription is NACKED
        // due to TCP endpoint not completely connected when subscription
        // is processed in the server - due to resubscribing the error handler
        // count may differ from expected value, but its not a real but as
        // the subscription takes places anyways and all events will be received.
        if (!subscription_error_occured_) {
            //ASSERT_EQ(subscribe_count, subscription_state_handler_called_);
        } else {
            VSOMEIP_WARNING << "Subscription state handler check skipped: CallCount="
                    << std::dec << subscription_state_handler_called_;
        }
    }

    void notify() {
        do {
            std::unique_lock<std::mutex> its_lock(notify_mutex_);
            while (wait_for_notify_) {
                notify_condition_.wait(its_lock);
            }
            wait_for_notify_ = true;
            // sleep a while before starting to notify this is necessary as it's not
            // possible to detect if _all_ clients on the remote side have
            // successfully subscribed as we only receive once subscription per
            // remote node no matter how many clients subscribed to this eventgroup
            // on the remote node
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));

            VSOMEIP_INFO << "[" << std::setw(4) << std::setfill('0') << std::hex
                    << service_info_.service_id << "] Starting to notify";

            for(uint32_t i = 0; i < subscribe_notify_test::notifications_to_send; i++) {
                std::shared_ptr<vsomeip::payload> its_payload =
                        vsomeip::runtime::get()->create_payload();

                vsomeip::byte_t its_data[10] = {0};
                for (uint32_t j = 0; j < i+1; ++j) {
                    its_data[j] = static_cast<uint8_t>(j);
                }
                its_payload->set_data(its_data, i+1);
                VSOMEIP_DEBUG << "[" << std::setw(4) << std::setfill('0') << std::hex
                    << service_info_.service_id << "] Notifying: " << i+1;
                app_->notify(service_info_.service_id, service_info_.instance_id,
                        service_info_.event_id, its_payload);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        } while(endless);
    }

    void wait_for_stop() {
        std::unique_lock<std::mutex> its_lock(stop_mutex_);
        while (wait_for_stop_) {
            stop_condition_.wait(its_lock);
        }

        // wait until all notifications have been sent out
        notify_thread_.join();

        VSOMEIP_INFO << "[" << std::setw(4) << std::setfill('0') << std::hex
                << service_info_.service_id
                << "] Received notifications from all other services, going down";

        // let offer thread exit
        {
            std::lock_guard<std::mutex> its_lock(mutex_);
            wait_until_notified_from_other_services_ = false;
            condition_.notify_one();
        }

        stop_offer();

        // ensure that the service which hosts the routing doesn't exit to early
        if (app_->is_routing()) {
            for (const auto& i : service_infos_) {
                if ((i.service_id == service_info_.service_id
                                && i.instance_id == service_info_.instance_id)
                        || (i.service_id == 0xFFFF && i.instance_id == 0xFFFF)) {
                    continue;
                }
                while (app_->is_available(i.service_id, i.instance_id,
                        vsomeip::ANY_MAJOR, vsomeip::ANY_MINOR)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        for(const auto& i : service_infos_) {
            if ((i.service_id == service_info_.service_id
                    && i.instance_id == service_info_.instance_id)
                    || (i.service_id == 0xFFFF && i.instance_id == 0xFFFF)) {
                continue;
            }
            app_->unregister_subscription_status_handler(i.service_id, i.instance_id,
                    i.eventgroup_id, vsomeip::ANY_EVENT);
            app_->unsubscribe(i.service_id, i.instance_id, i.eventgroup_id);
            app_->release_event(i.service_id, i.instance_id, i.event_id);
            app_->release_service(i.service_id, i.instance_id);
        }
        app_->clear_all_handler();
        app_->stop();
    }

private:
    subscribe_notify_test::service_info service_info_;
    std::array<subscribe_notify_test::service_info, 7> service_infos_;
    std::shared_ptr<vsomeip::application> app_;
    std::map<std::pair<vsomeip::service_t, vsomeip::instance_t>, bool> other_services_available_;
    std::map<std::pair<vsomeip::service_t, vsomeip::method_t>, std::uint32_t> other_services_received_notification_;

    std::map<std::pair<vsomeip::service_t, vsomeip::method_t>, std::uint32_t> other_services_received_request_;
    std::map<std::pair<vsomeip::service_t, vsomeip::method_t>, std::uint32_t> other_services_received_response_;

    bool wait_until_registered_;
    bool wait_until_other_services_available_;
    bool wait_until_notified_from_other_services_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread offer_thread_;

    bool wait_for_stop_;
    std::mutex stop_mutex_;
    std::condition_variable stop_condition_;
    std::thread stop_thread_;

    std::set<vsomeip::client_t> subscribers_;
    bool wait_for_notify_;
    std::mutex notify_mutex_;
    std::condition_variable notify_condition_;
    std::thread notify_thread_;
    std::atomic<uint32_t> subscription_state_handler_called_;
    std::atomic<bool> subscription_error_occured_;

    std::mutex subscribers_mutex_;
    vsomeip::reliability_type_e reliability_type_;

    bool wait_until_unsubscribe_;
    std::mutex wait_unsubscribe_mutex_;
    std::condition_variable wait_unsubscribe_condition_;

    std::set<vsomeip::client_t> sub_services_;
};

static unsigned long service_number;
static bool use_same_service_id;
vsomeip::reliability_type_e reliability_type = vsomeip::reliability_type_e::RT_UNKNOWN;

#ifndef _WIN32
int main(int argc, char** argv)
{
    if(argc < 2) {
        std::cerr << "Please specify a service number and event reliability type, like: " << argv[0] << " 2 UDP SAME_SERVICE_ID" << std::endl;
        std::cerr << "Valid service numbers are in the range of [1,6]" << std::endl;
        std::cerr << "Valid event reliability types are [UDP, TCP, TCP_AND_UDP]" << std::endl;
        std::cerr << "If SAME_SERVICE_ID is specified as third parameter the test is run w/ multiple instances of the same service" << std::endl;
        return 1;
    }

    service_number = std::stoul(std::string(argv[1]), nullptr);

    if (argc >= 3) {
        if (std::string("TCP")== std::string(argv[2])) {
            reliability_type = vsomeip::reliability_type_e::RT_RELIABLE;
        } else if (std::string("UDP")== std::string(argv[2])) {
            reliability_type = vsomeip::reliability_type_e::RT_UNRELIABLE;
        } else if (std::string("TCP_AND_UDP")== std::string(argv[2])) {
            reliability_type = vsomeip::reliability_type_e::RT_BOTH;
        } 
    }

    if (argc >= 4 && std::string("SAME_SERVICE_ID") == std::string(argv[3])) {
        use_same_service_id = true;
    } else {
        use_same_service_id = false;
    }

    int i = 1;
    while (i < argc )
    {
        if(std::string("--endless") == std::string(argv[i])) {
            endless = true;
            break;
        }
        i++;
    }

    if(use_same_service_id) {
        subscribe_notify_test_service its_sample(
                subscribe_notify_test::service_infos_same_service_id[service_number],
                subscribe_notify_test::service_infos_same_service_id,
                reliability_type);
    } else {
        subscribe_notify_test_service its_sample(
                subscribe_notify_test::service_infos[service_number],
                subscribe_notify_test::service_infos,
                reliability_type);
    }
}
#endif
