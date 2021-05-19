--local_ip 10.0.0.214 --adapter EFA
--log_component "PROBE ENDPOINT_MANAGER PERFORMANCE_METRICS"
--log_level DEBUG
--stderr

# AVM test Rx Demux stream ID 3.
-X --rx_buffer_delay 0 --stats_period 1 --num_transactions 100 --rx AVM --dest_port 3300 --rate 60
-S --id 3 --payload_size 50 --pattern INC --pat_start 1 --avm_anc
