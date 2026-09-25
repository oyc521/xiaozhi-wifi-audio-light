W (2080) Display: Role:system
W (2080) Display:      
W (2080) Display: SetEmotion: microchip_ai
I (2090) Application: STATE: activating
W (2090) Display: SetStatus: 检查新版本...
I (2090) Ota: Current version: 2.0.3
I (2480) esp-x509-crt-bundle: Certificate validated
E (3360) Dynamic Impl: mbedtls_ssl_fetch_input error=76
E (3360) esp-tls-mbedtls: read error :-0x004C
E (3360) EspSsl: SSL receive failed: -76
I (3370) Ota: Current is the latest version
I (3370) Ota: Running partition: ota_0
W (3370) Display: SetStatus: 登录服务器...
I (3380) MCP: Add tool: self.get_device_status
I (3380) MCP: Add tool: self.audio_speaker.set_volume
I (3390) MCP: Add tool: self.get_system_info [user]
I (3390) MCP: Add tool: self.reboot [user]
I (3390) MCP: Add tool: self.upgrade_firmware [user]
I (3400) MCP: Add tool: self.assets.set_download_url [user]
I (3410) MQTT: Connecting to endpoint api.tenclass.net
I (3650) esp-x509-crt-bundle: Certificate validated
I (4340) MQTT: Connected to endpoint
I (4340) SystemInfo: free sram: 70495 minimal sram: 39812
I (4340) Application: STATE: idle
W (4340) Display: SetStatus: 待命
W (4340) Display: SetEmotion: neutral
I (4340) AfeWakeWord: Model 0: wn9_nihaoxiaozhi_tts
I (4350) AFE_CONFIG: Set WakeNet Model: wn9_nihaoxiaozhi_tts
MC Quantized wakenet9: wakenet9l_tts1h8_你好小智_3_0.631_0.635, tigger:v4, mode:0, p:0, (Aug 11 2025 15:20:50)
I (4420) AFE: AFE Version: (1MIC_V250121)
I (4420) AFE: Input PCM Config: total 1 channels(1 microphone, 0 playback), sample rate:16000
I (4430) AFE: AFE Pipeline: [input] ->  -> |VAD(WebRTC)| -> |WakeNet(wn9_nihaoxiaozhi_tts,)| -> [output]
I (4440) AfeWakeWord: Audio detection task started, feed size: 512 fetch size: 512
W (4450) Display: ShowNotification: 版本 2.0.3
W (4450) Display: Role:system
W (4460) Display:      
I (4460) AudioService: OpusHead: version=1, channels=1, sample_rate=16000
I (4460) AudioService: Resampling audio from 16000 to 24000
I (4470) OpusResampler: Resampler configured with input sample rate 16000 and output sample rate 24000
I (4490) main_task: Returned from app_main()
W (6920) httpd_txrx: httpd_sock_err: error in recv : 104
W (7000) httpd_txrx: httpd_sock_err: error in recv : 104
I (13510) SystemInfo: free sram: 58147 minimal sram: 33468
I (21510) AudioCodec: Set output enable to false
I (23510) SystemInfo: free sram: 57983 minimal sram: 33468
W (24560) httpd_txrx: httpd_sock_err: error in recv : 104
W (24920) httpd_txrx: httpd_sock_err: error in recv : 104
W (30050) httpd_txrx: httpd_sock_err: error in recv : 104
W (31300) httpd_txrx: httpd_sock_err: error in recv : 104
W (33110) httpd_txrx: httpd_sock_err: error in recv : 104
I (33510) SystemInfo: free sram: 58443 minimal sram: 33468
W (34190) httpd_txrx: httpd_sock_err: error in recv : 104
I (36890) LED_CTRL: LED模式设置为: 1
I (39060) LED_CTRL: LED模式设置为: 0
I (40280) LED_CTRL: LED模式设置为: 5
I (42140) LED_CTRL: LED模式设置为: 15
I (43510) SystemInfo: free sram: 57999 minimal sram: 33468
W (43810) httpd_txrx: httpd_sock_err: error in recv : 104
I (50540) LED_CTRL: LED模式设置为: 14
I (51710) LED_CTRL: LED模式设置为: 15
I (52460) LED_CTRL: LED模式设置为: 16
I (53510) SystemInfo: free sram: 57631 minimal sram: 33468
I (54020) LED_CTRL: LED模式设置为: 17
I (55320) LED_CTRL: LED模式设置为: 0
I (56660) LED_CTRL: LED模式设置为: 1
W (59320) httpd_txrx: httpd_sock_err: error in recv : 104
I (59810) LED_CTRL: LED模式设置为: 2
I (62610) LED_CTRL: LED模式设置为: 0
W (62620) httpd_txrx: httpd_sock_err: error in recv : 104
I (63510) SystemInfo: free sram: 58519 minimal sram: 33468
W (63620) httpd_txrx: httpd_sock_err: error in recv : 104
W (67300) httpd_txrx: httpd_sock_err: error in recv : 104
W (67930) httpd_txrx: httpd_sock_err: error in recv : 104
I (73510) SystemInfo: free sram: 57979 minimal sram: 33468
I (83510) SystemInfo: free sram: 57315 minimal sram: 33468
W (84710) httpd_txrx: httpd_sock_err: error in recv : 104
I (87030) Application: STATE: connecting
W (87030) Display: SetStatus: 连接中...
W (87030) Display: SetEmotion: neutral
W (87030) Display: Role:system
W (87030) Display:      
I (87170) MQTT: Session ID: edc96b3e
I (87170) WifiStation: Setting WiFi power save level: PERFORMANCE (NONE)
I (87170) wifi:Set ps type: 0, coexist: 0

I (87170) Application: Wake word detected: 你好小智
I (87440) AfeWakeWord: Encode wake word opus 66 packets in 410 ms
I (87440) Application: STATE: listening
W (87440) Display: SetStatus: 聆听中...
W (87440) Display: SetEmotion: neutral
I (87450) AFE: AFE Version: (1MIC_V250121)
I (87450) AFE: Input PCM Config: total 1 channels(1 microphone, 0 playback), sample rate:16000
I (87460) AFE: AFE Pipeline: [input] -> |VAD(WebRTC)| -> [output]
I (87470) AfeAudioProcessor: Audio communication task started, feed size: 512 fetch size: 512
I (87570) Application: >> 你好小智
W (87570) Display: Role:user
W (87570) Display:      你好小智
I (87590) Application: STATE: speaking
W (87590) Display: SetStatus: 说话中...
W (88310) Display: SetEmotion: happy
I (88310) Application: << 你好呀～
W (88310) Display: Role:assistant
W (88310) Display:      你好呀～
I (88640) AudioCodec: Set output enable to true
I (89700) Application: << 我在这儿呢，今天过得怎么样？
W (89700) Display: Role:assistant
W (89700) Display:      我在这儿呢，今天过得怎么样？
I (92220) Application: STATE: listening
W (92220) Display: SetStatus: 聆听中...
W (92220) Display: SetEmotion: neutral
I (101510) SystemInfo: free sram: 46687 minimal sram: 33468
I (107640) AudioCodec: Set output enable to false
I (111510) SystemInfo: free sram: 46687 minimal sram: 33468
I (121510) SystemInfo: free sram: 46715 minimal sram: 33