#
# Copyright (c) 2023 Wind River Systems, Inc.
#

This example demonstrates request/response between 2 clients and 2 services. 
To use the example applications in VxWorks you need two devices running VxWorks on the same network. 
The network addresses within the configuration files need to be adapted to match the devices addresses.

At the first VxWorks device with ROMFS, start the 2 payload test services:
cmd
cd /romfs
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/payload_test_service_multi.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=payload_test_service_1" /romfs/payload_test_service 1 &
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/payload_test_service_multi.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=payload_test_service_2" /romfs/payload_test_service 2

At the second VxWorks device with ROMFS, start the 2 payload test clients:
cmd
cd /romfs
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/payload_test_client_multi.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=payload_test_client_1" /romfs/payload_test_client -- --udp --max-payload-size UDP &
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/payload_test_client_multi.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=payload_test_client_2" /romfs/payload_test_client -- --udp --max-payload-size UDP
