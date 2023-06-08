#
# Copyright (c) 2023 Wind River Systems, Inc.
#
# Copyright (C) 2015-2017 Bayerische Motoren Werke Aktiengesellschaft (BMW AG)
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

cmake_minimum_required (VERSION 2.8)

set(EXAMPLE_CONFIG_FILES
    "../config/vsomeip.json"
    "../config/vsomeip-local.json"
    "../config/vsomeip-tcp-client.json"
    "../config/vsomeip-tcp-service.json"
    "../config/vsomeip-udp-client.json"
    "../config/vsomeip-udp-service.json"
)

# Examples
add_executable(request-sample request-sample.cpp ${EXAMPLE_CONFIG_FILES})
target_link_libraries(request-sample ${VSOMEIP_NAME} ${Boost_LIBRARIES} ${DL_LIBRARY})

add_executable(response-sample response-sample.cpp ${EXAMPLE_CONFIG_FILES})
target_link_libraries(response-sample ${VSOMEIP_NAME} ${Boost_LIBRARIES} ${DL_LIBRARY})

add_executable(subscribe-sample subscribe-sample.cpp ${EXAMPLE_CONFIG_FILES})
target_link_libraries(subscribe-sample ${VSOMEIP_NAME} ${Boost_LIBRARIES} ${DL_LIBRARY})

add_executable(notify-sample notify-sample.cpp ${EXAMPLE_CONFIG_FILES})
target_link_libraries(notify-sample ${VSOMEIP_NAME} ${Boost_LIBRARIES} ${DL_LIBRARY})

add_executable(payload_test_client
    payload_tests/payload_test_client.cpp
)
target_link_libraries(payload_test_client
    vsomeip3
    ${Boost_LIBRARIES}
    ${DL_LIBRARY}
)

add_executable(payload_test_service
    payload_tests/payload_test_service.cpp
)
target_link_libraries(payload_test_service
    vsomeip3
    ${Boost_LIBRARIES}
    ${DL_LIBRARY}
)

set(TEST_SUBSCRIBE_NOTIFY_NAME subscribe_notify_test)
set(TEST_SUBSCRIBE_NOTIFY_SERVICE ${TEST_SUBSCRIBE_NOTIFY_NAME}_service)
add_executable(${TEST_SUBSCRIBE_NOTIFY_SERVICE} subscribe_notify_tests/${TEST_SUBSCRIBE_NOTIFY_NAME}_service.cpp)
target_link_libraries(${TEST_SUBSCRIBE_NOTIFY_SERVICE}
    vsomeip3
    ${Boost_LIBRARIES}
    ${DL_LIBRARY}
)

add_dependencies(examples request-sample response-sample subscribe-sample notify-sample payload_test_client payload_test_service subscribe_notify_test_service)

install (
    TARGETS request-sample response-sample subscribe-sample notify-sample payload_test_client subscribe_notify_test_service
    RUNTIME DESTINATION "${INSTALL_BIN_DIR}" COMPONENT bin
)

###################################################################################################
