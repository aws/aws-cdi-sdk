--adapter EFA --local_ip 10.0.0.212
--log_component "PROBE ENDPOINT_MANAGER PERFORMANCE_METRICS"
--log_level DEBUG
--stderr

# ----------------------------------------------------------------------------
# Non-demux test using legacy APIs. Single TX to single Rx. Use a single Rx
# cdi_test listening on single port.
# ----------------------------------------------------------------------------
-XS -name TX1 --num_transactions 100 --tx AVM --keep_alive --rate 60
-S --id 1 --remote_ip 10.0.0.214 --dest_port 3100 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
