#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
stream_agent.py — 常驻守护：监听设备"音乐模式"，自动开始/停止 WASAPI 回环推流（可选自动放歌）

用法:
    python stream_agent.py [设备IP] [--play 播放目标] [--always] [--port 5004] [--poll 1.0]

    --play <路径或网址>   进入音乐模式时自动打开（歌单文件 / 音乐 App / 网页）
    --always              不论音乐模式，启动即一直推流（等同旧 loopback 行为）
    --port <n>            UDP 推流端口（默认 5004）
    --poll <秒>           状态轮询间隔（默认 1.0s）

依赖: pip install pyaudiowpatch numpy

说明:
    - 设备端已提供 GET /api/status（含 music_mode 字段），本脚本无需改固件。
    - 你在网页/语音切到"音乐模式" -> 本脚本自动开始推流(+放歌)；退出则停止。
"""
import sys, os, time, json, socket
import urllib.request

import numpy as np
import pyaudiowpatch as pa

SR_OUT = 16000
CHUNK = 512                    # samples per UDP packet (== 设备 FFT_SIZE)
DISCOVER_PORT = 5005
AUDIO_PORT = 5004
CACHE_FILE = os.path.join(os.path.expanduser('~'), '.esp_led_ip.txt')


# ---------------- 设备发现 ----------------
def _probe(ip, timeout=1.5):
    try:
        with urllib.request.urlopen('http://%s/api/status' % ip, timeout=timeout):
            return True
    except Exception:
        return False


def _read_cache():
    try:
        return open(CACHE_FILE).read().strip()
    except Exception:
        return ''


def _write_cache(ip):
    try:
        open(CACHE_FILE, 'w').write(ip)
    except Exception:
        pass


def discover_ip(timeout_s=2.5):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.settimeout(0.5)
    end = time.time() + timeout_s
    try:
        while time.time() < end:
            try:
                s.sendto(b'ESPLED', ('<broadcast>', DISCOVER_PORT))
                data, addr = s.recvfrom(128)
            except socket.timeout:
                continue
            parts = data.decode('utf-8', 'ignore').split()
            if len(parts) >= 2 and parts[0] == 'ESPLED':
                return parts[1]
    finally:
        s.close()
    return None


def resolve_ip(arg_ip):
    if arg_ip:
        return arg_ip
    c = _read_cache()
    if c and _probe(c):
        print('使用上次的设备 IP:', c)
        return c
    print('正在局域网自动发现设备（若弹窗询问防火墙，请点"允许访问"）...')
    ip = discover_ip()
    if ip:
        _write_cache(ip)
        print('发现设备:', ip)
        return ip
    return None


def get_status(ip, timeout=1.5):
    try:
        with urllib.request.urlopen('http://%s/api/status' % ip, timeout=timeout) as r:
            return json.loads(r.read().decode('utf-8', 'ignore'))
    except Exception:
        return None


def auto_play(target):
    if not target:
        return
    try:
        if os.name == 'nt':
            os.startfile(target)                       # noqa
        else:
            import subprocess
            subprocess.Popen(['xdg-open', target])
        print('♪ 已打开播放目标:', target)
    except Exception as e:
        print('[!] 自动播放失败:', e)


# ---------------- 推流器 ----------------
class LoopbackStreamer:
    def __init__(self, ip, port):
        self.ip = ip
        self.port = port
        self.active = False
        self._pa = None
        self._stream = None
        self._sock = None
        self._ring = bytearray()
        self._pos = 0.0

    def start(self):
        if self.active:
            return True
        self._pa = pa.PyAudio()
        try:
            lo = self._pa.get_default_wasapi_loopback()
        except Exception as e:
            print('[!] 未找到 WASAPI 回环设备:', e)
            try:
                for d in self._pa.get_loopback_device_info_generator():
                    print('    [%s] %s  %dHz' % (d['index'], d['name'], d['defaultSampleRate']))
            except Exception:
                pass
            self._pa.terminate()
            self._pa = None
            return False

        rate = int(lo['defaultSampleRate'])
        ch = int(lo['maxInputChannels']) or 2
        ratio = SR_OUT / rate
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._ring = bytearray()
        self._pos = 0.0

        def cb(in_data, frame_count, time_info, status):
            data = np.frombuffer(in_data, dtype=np.float32).reshape(-1, ch)
            mono = data.mean(axis=1)
            n_in = len(mono)
            idx = self._pos
            out = []
            while idx + 1 < n_in:                       # 线性重采样到 16k
                i0 = int(idx)
                t = idx - i0
                out.append(mono[i0] * (1 - t) + mono[i0 + 1] * t)
                idx += ratio
            self._pos = idx - n_in
            if out:
                s16 = (np.clip(np.array(out, dtype=np.float32), -1, 1) * 32767).astype('<i2')
                self._ring.extend(s16.tobytes())
                while len(self._ring) >= CHUNK * 2:
                    self._sock.sendto(bytes(self._ring[:CHUNK * 2]), (self.ip, self.port))
                    del self._ring[:CHUNK * 2]
            return (None, pa.paContinue)

        self._stream = self._pa.open(format=pa.paFloat32, channels=ch, rate=rate,
                                     input=True, input_device_index=lo['index'],
                                     frames_per_buffer=1024, stream_callback=cb)
        self._stream.start_stream()
        self.active = True
        print('▶ 开始推流 -> %s:%d  (回环 %dHz %dch)' % (self.ip, self.port, rate, ch))
        return True

    def stop(self):
        if not self.active:
            return
        try:
            self._stream.stop_stream()
            self._stream.close()
        except Exception:
            pass
        try:
            self._pa.terminate()
        except Exception:
            pass
        try:
            self._sock.close()
        except Exception:
            pass
        self._pa = None
        self._stream = None
        self._sock = None
        self.active = False
        print('■ 已停止推流')


# ---------------- 入口 ----------------
def parse_args(argv):
    ip = None
    play = None
    port = AUDIO_PORT
    poll = 1.0
    always = False
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--play' and i + 1 < len(argv):
            play = argv[i + 1]; i += 2
        elif a == '--port' and i + 1 < len(argv):
            port = int(argv[i + 1]); i += 2
        elif a == '--poll' and i + 1 < len(argv):
            poll = float(argv[i + 1]); i += 2
        elif a == '--always':
            always = True; i += 1
        elif not a.startswith('--') and ip is None:
            ip = a; i += 1
        else:
            i += 1
    return ip, play, port, poll, always


def main():
    ip_arg, play, port, poll, always = parse_args(sys.argv[1:])
    ip = resolve_ip(ip_arg)
    if not ip:
        print('未发现设备。请确认设备已上电联网、与电脑同一网络；或手动指定：')
        print('    python stream_agent.py <设备IP>')
        print('    (设备 IP 可看路由器 DHCP 列表，或 GET http://<ip>/api/status)')
        sys.exit(2)
    _write_cache(ip)

    streamer = LoopbackStreamer(ip, port)

    if always:
        print('--always：直接开始推流（CTRL+C 停止）')
        streamer.start()
        try:
            while True:
                time.sleep(0.2)
        except KeyboardInterrupt:
            pass
        finally:
            streamer.stop()
        return

    print('守护中：等待设备进入「音乐模式」...（CTRL+C 退出）')
    print('   设备 IP: %s   推流端口: %d   轮询: %.1fs' % (ip, port, poll))
    was = False
    miss = 0
    try:
        while True:
            st = get_status(ip)
            if st is None:
                miss += 1
                if miss == 1 or miss % 15 == 0:
                    print('[!] 读不到设备状态（第 %d 次），确认设备在线/同网' % miss)
                time.sleep(poll)
                continue
            if miss:
                print('设备已恢复: %s' % ip)
                miss = 0
            music = bool(st.get('music_mode'))
            if music and not was:
                print('检测到「音乐模式」ON')
                auto_play(play)
                streamer.start()
            elif (not music) and was:
                print('检测到「音乐模式」OFF')
                streamer.stop()
            was = music
            time.sleep(poll)
    except KeyboardInterrupt:
        pass
    finally:
        streamer.stop()
        print('已退出')


if __name__ == '__main__':
    main()
