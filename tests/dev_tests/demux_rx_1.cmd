--local_ip 10.0.0.214 --adapter EFA
--log_component "PROBE ENDPOINT_MANAGER PERFORMANCE_METRICS"
--log_level DEBUG
--stderr

# AVM test Rx Demux stream ID 1.
-X --rx_buffer_delay 0 --stats_period 1 --num_transactions 100 --rx AVM --dest_port 3100 --rate 60
-S --id 1 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
