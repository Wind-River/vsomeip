#
# Copyright (c) 2023 Wind River Systems, Inc.
#

This example demonstrates subscribe/notify between 3 clients and 3 services. 
To use the example applications in VxWorks you need two devices running VxWorks on the same network. 
The network addresses within the configuration files need to be adapted to match the devices addresses.

At the first VxWorks device with ROMFS, start the 2 payload test services:
cmd
cd /romfs
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/subscribe_notify_master.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=subscribe_notify_test_service_one" /romfs/subscribe_notify_test_service -- 1 UDP &
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/subscribe_notify_master.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=subscribe_notify_test_service_two" /romfs/subscribe_notify_test_service -- 2 UDP &
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/subscribe_notify_master.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=subscribe_notify_test_service_three" /romfs/subscribe_notify_test_service -- 3 UDP

At the second VxWorks device with ROMFS, start the 2 payload test clients:
cmd
cd /romfs
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/subscribe_notify_slave.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=subscribe_notify_test_service_four" /romfs/subscribe_notify_test_service -- 4 UDP &
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/subscribe_notify_slave.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=subscribe_notify_test_service_five" /romfs/subscribe_notify_test_service -- 5 UDP &
rtp exec -e "LD_LIBRARY_PATH=/romfs,VSOMEIP_CONFIGURATION=/romfs/subscribe_notify_slave.json,VSOMEIP_CFG_LIBRARY=/romfs/libvsomeip3-cfg.so.3,VSOMEIP_APPLICATION_NAME=subscribe_notify_test_service_six" /romfs/subscribe_notify_test_service -- 6 UDP