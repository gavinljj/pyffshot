#!/usr/bin/env python
# -*- coding: utf-8 -*-
from ctypes import cdll
import os
import sys
__path = os.path.dirname(os.path.abspath(__file__))
cdll.LoadLibrary(__path + "/lib/libavutil.so")
cdll.LoadLibrary(__path + "/lib/libswresample.so")
cdll.LoadLibrary(__path + "/lib/libswscale.so")
cdll.LoadLibrary(__path + "/lib/libavcodec.so")
cdll.LoadLibrary(__path + "/lib/libavformat.so")
cdll.LoadLibrary(__path + "/lib/libavfilter.so")
cdll.LoadLibrary(__path + "/lib/libavdevice.so")
__libshot = cdll.LoadLibrary(__path + "/lib/libshot.so")

def shot(url, output, image_codec_name="mjpeg", timeout=5000):
    """
    从指定的url视频中截取第一个关键帧画面
    :param url: 视频url，可以为本地文件地址，也可以为网络url
    :param output: 截图输出的本地文件路径
    :param image_codec_name: 截图使用的ffmpeg对应的codec_name
    :param timeout: 连接超时设定，不支持rtmp协议, 单位ms
    :return:
    """
    timeout = 0 if url.startswith("rtmp") else timeout
    return __libshot.shot(url, image_codec_name, output, timeout)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print "Usage:\n\tpython shot.py URL IMAGE_PATH\n"
        exit(-1)
    shot(sys.argv[1], sys.argv[2])
