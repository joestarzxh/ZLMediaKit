﻿/*
 * Copyright (c) 2016 The ZLMediaKit project authors. All Rights Reserved.
 *
 * This file is part of ZLMediaKit(https://github.com/xiongziliang/ZLMediaKit).
 *
 * Use of this source code is governed by MIT license that can be found in the
 * LICENSE file in the root of the source tree. All contributing project authors
 * may be found in the AUTHORS file in the root of the source tree.
 */

#if defined(ENABLE_RTPPROXY)
#include "PSEncoder.h"
#include "Extension/H264.h"
namespace mediakit{

PSEncoder::PSEncoder() {
    _buffer = std::make_shared<BufferRaw>();
    init();
}

PSEncoder::~PSEncoder() {

}

void PSEncoder::init() {
    static struct ps_muxer_func_t func = {
            /*alloc*/
            [](void *param, size_t bytes) {
                PSEncoder *thiz = (PSEncoder *) param;
                thiz->_buffer->setCapacity(bytes + 1);
                return (void *) thiz->_buffer->data();
            },
            /*free*/
            [](void *param, void *packet) {
                //什么也不做
            },
            /*wtite*/
            [](void *param, int stream, void *packet, size_t bytes) {
                PSEncoder *thiz = (PSEncoder *) param;
                thiz->onPS(thiz->_timestamp, packet, bytes);
            }
    };

    _muxer.reset(ps_muxer_create(&func, this), [](struct ps_muxer_t *ptr) {
        ps_muxer_destroy(ptr);
    });
}

void PSEncoder::addTrack(const Track::Ptr &track) {
    switch (track->getCodecId()) {
        case CodecH264: {
            _codec_to_trackid[track->getCodecId()].track_id = ps_muxer_add_stream(_muxer.get(), STREAM_VIDEO_H264, nullptr, 0);
            break;
        }

        case CodecH265: {
            _codec_to_trackid[track->getCodecId()].track_id = ps_muxer_add_stream(_muxer.get(), STREAM_VIDEO_H265, nullptr, 0);
            break;
        }

        case CodecAAC: {
            _codec_to_trackid[track->getCodecId()].track_id = ps_muxer_add_stream(_muxer.get(), STREAM_AUDIO_AAC, nullptr, 0);
            break;
        }

        case CodecG711A: {
            _codec_to_trackid[track->getCodecId()].track_id = ps_muxer_add_stream(_muxer.get(), STREAM_AUDIO_G711A, nullptr, 0);
            break;
        }

        case CodecG711U: {
            _codec_to_trackid[track->getCodecId()].track_id = ps_muxer_add_stream(_muxer.get(), STREAM_AUDIO_G711U, nullptr, 0);
            break;
        }

        case CodecOpus: {
            _codec_to_trackid[track->getCodecId()].track_id = ps_muxer_add_stream(_muxer.get(), STREAM_AUDIO_OPUS, nullptr, 0);
            break;
        }

        default: WarnL << "mpeg-ps 不支持该编码格式,已忽略:" << track->getCodecName(); break;
    }
    //尝试音视频同步
    stampSync();
}

void PSEncoder::stampSync(){
    if(_codec_to_trackid.size() < 2){
        return;
    }

    Stamp *audio = nullptr, *video = nullptr;
    for(auto &pr : _codec_to_trackid){
        switch (getTrackType((CodecId) pr.first)){
            case TrackAudio : audio = &pr.second.stamp; break;
            case TrackVideo : video = &pr.second.stamp; break;
            default : break;
        }
    }

    if(audio && video){
        //音频时间戳同步于视频，因为音频时间戳被修改后不影响播放
        audio->syncTo(*video);
    }
}

void PSEncoder::resetTracks() {
    init();
}

void PSEncoder::inputFrame(const Frame::Ptr &frame) {
    auto it = _codec_to_trackid.find(frame->getCodecId());
    if (it == _codec_to_trackid.end()) {
        return;
    }
    auto &track_info = it->second;
    int64_t dts_out, pts_out;
    switch (frame->getCodecId()) {
        case CodecH264: {
            int type = H264_TYPE(*((uint8_t *) frame->data() + frame->prefixSize()));
            if (type == H264Frame::NAL_SEI) {
                break;
            }
        }
        case CodecH265: {
            //这里的代码逻辑是让SPS、PPS、IDR这些时间戳相同的帧打包到一起当做一个帧处理，
            if (!_frameCached.empty() && _frameCached.back()->dts() != frame->dts()) {
                Frame::Ptr back = _frameCached.back();
                Buffer::Ptr merged_frame = back;
                if (_frameCached.size() != 1) {
                    string merged;
                    _frameCached.for_each([&](const Frame::Ptr &frame) {
                        if (frame->prefixSize()) {
                            merged.append(frame->data(), frame->size());
                        } else {
                            merged.append("\x00\x00\x00\x01", 4);
                            merged.append(frame->data(), frame->size());
                        }
                    });
                    merged_frame = std::make_shared<BufferString>(std::move(merged));
                }
                track_info.stamp.revise(back->dts(), back->pts(), dts_out, pts_out);
                _timestamp = dts_out;
                ps_muxer_input(_muxer.get(), track_info.track_id, back->keyFrame() ? 0x0001 : 0, pts_out * 90LL,
                               dts_out * 90LL, merged_frame->data(), merged_frame->size());
                _frameCached.clear();
            }
            _frameCached.emplace_back(Frame::getCacheAbleFrame(frame));
        }
            break;

        case CodecAAC: {
            if (frame->prefixSize() == 0) {
                WarnL << "必须提供adts头才能mpeg-ps打包";
                break;
            }
        }

        default: {
            track_info.stamp.revise(frame->dts(), frame->pts(), dts_out, pts_out);
            _timestamp = dts_out;
            ps_muxer_input(_muxer.get(), track_info.track_id, frame->keyFrame() ? 0x0001 : 0, pts_out * 90LL,
                           dts_out * 90LL, frame->data(), frame->size());
        }
            break;
    }
}

}//namespace mediakit
#endif//defined(ENABLE_RTPPROXY)
