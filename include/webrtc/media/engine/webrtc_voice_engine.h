/*
 *  Copyright (c) 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MEDIA_ENGINE_WEBRTC_VOICE_ENGINE_H_
#define MEDIA_ENGINE_WEBRTC_VOICE_ENGINE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "call/audio_state.h"
#include "call/call.h"
#include "media/base/rtp_utils.h"
#include "media/engine/apm_helpers.h"
#include "modules/audio_processing/include/audio_processing.h"
#include "pc/channel.h"
#include "rtc_base/buffer.h"
#include "rtc_base/constructor_magic.h"
#include "rtc_base/experiments/audio_allocation_settings.h"
#include "rtc_base/network_route.h"
#include "rtc_base/task_queue.h"
#include "rtc_base/thread_checker.h"

namespace cricket {

class AudioDeviceModule;
class AudioMixer;
class AudioSource;
class WebRtcVoiceMediaChannel;

// WebRtcVoiceEngine is a class to be used with CompositeMediaEngine.
// It uses the WebRtc VoiceEngine library for audio handling.
class WebRtcVoiceEngine final : public VoiceEngineInterface {
  friend class WebRtcVoiceMediaChannel;

 public:
  WebRtcVoiceEngine(
      webrtc::AudioDeviceModule* adm,
      const rtc::scoped_refptr<webrtc::AudioEncoderFactory>& encoder_factory,
      const rtc::scoped_refptr<webrtc::AudioDecoderFactory>& decoder_factory,
      rtc::scoped_refptr<webrtc::AudioMixer> audio_mixer,
      rtc::scoped_refptr<webrtc::AudioProcessing> audio_processing);
  ~WebRtcVoiceEngine() override;

  // Does initialization that needs to occur on the worker thread.
  void Init() override;

  rtc::scoped_refptr<webrtc::AudioState> GetAudioState() const override;
  VoiceMediaChannel* CreateMediaChannel(
      webrtc::Call* call,
      const MediaConfig& config,
      const AudioOptions& options,
      const webrtc::CryptoOptions& crypto_options) override;

  const std::vector<AudioCodec>& send_codecs() const override;
  const std::vector<AudioCodec>& recv_codecs() const override;
  RtpCapabilities GetCapabilities() const override;

  // For tracking WebRtc channels. Needed because we have to pause them
  // all when switching devices.
  // May only be called by WebRtcVoiceMediaChannel.
  void RegisterChannel(WebRtcVoiceMediaChannel* channel);
  void UnregisterChannel(WebRtcVoiceMediaChannel* channel);

  // Starts AEC dump using an existing file. A maximum file size in bytes can be
  // specified. When the maximum file size is reached, logging is stopped and
  // the file is closed. If max_size_bytes is set to <= 0, no limit will be
  // used.
  bool StartAecDump(rtc::PlatformFile file, int64_t max_size_bytes) override;

  // Stops AEC dump.
  void StopAecDump() override;

  const webrtc::AudioProcessing::Config GetApmConfigForTest() const {
    return apm()->GetConfig();
  }

 private:
  // Every option that is "set" will be applied. Every option not "set" will be
  // ignored. This allows us to selectively turn on and off different options
  // easily at any time.
  bool ApplyOptions(const AudioOptions& options);

  void StartAecDump(const std::string& filename);
  int CreateVoEChannel();

  std::unique_ptr<rtc::TaskQueue> low_priority_worker_queue_;

  webrtc::AudioDeviceModule* adm();
  webrtc::AudioProcessing* apm() const;
  webrtc::AudioState* audio_state();

  AudioCodecs CollectCodecs(
      const std::vector<webrtc::AudioCodecSpec>& specs) const;

  rtc::ThreadChecker signal_thread_checker_;
  rtc::ThreadChecker worker_thread_checker_;

  const webrtc::AudioAllocationSettings allocation_settings_;

  // The audio device module.
  rtc::scoped_refptr<webrtc::AudioDeviceModule> adm_;
  rtc::scoped_refptr<webrtc::AudioEncoderFactory> encoder_factory_;
  rtc::scoped_refptr<webrtc::AudioDecoderFactory> decoder_factory_;
  rtc::scoped_refptr<webrtc::AudioMixer> audio_mixer_;
  // The audio processing module.
  rtc::scoped_refptr<webrtc::AudioProcessing> apm_;
  // The primary instance of WebRtc VoiceEngine.
  rtc::scoped_refptr<webrtc::AudioState> audio_state_;
  std::vector<AudioCodec> send_codecs_;
  std::vector<AudioCodec> recv_codecs_;
  std::vector<WebRtcVoiceMediaChannel*> channels_;
  bool is_dumping_aec_ = false;
  bool initialized_ = false;

  webrtc::AgcConfig default_agc_config_;
  // Cache received extended_filter_aec, delay_agnostic_aec and experimental_ns
  // values, and apply them in case they are missing in the audio options.
  // We need to do this because SetExtraOptions() will revert to defaults for
  // options which are not provided.
  absl::optional<bool> extended_filter_aec_;
  absl::optional<bool> delay_agnostic_aec_;
  absl::optional<bool> experimental_ns_;
  // Jitter buffer settings for new streams.
  size_t audio_jitter_buffer_max_packets_ = 50;
  bool audio_jitter_buffer_fast_accelerate_ = false;
  int audio_jitter_buffer_min_delay_ms_ = 0;
  bool audio_jitter_buffer_enable_rtx_handling_ = false;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(WebRtcVoiceEngine);
};

// WebRtcVoiceMediaChannel is an implementation of VoiceMediaChannel that uses
// WebRtc Voice Engine.
class WebRtcVoiceMediaChannel final : public VoiceMediaChannel,
                                      public webrtc::Transport {
 public:
  WebRtcVoiceMediaChannel(WebRtcVoiceEngine* engine,
                          const MediaConfig& config,
                          const AudioOptions& options,
                          const webrtc::CryptoOptions& crypto_options,
                          webrtc::Call* call);
  ~WebRtcVoiceMediaChannel() override;

  const AudioOptions& options() const { return options_; }

  rtc::DiffServCodePoint PreferredDscp() const override;

  bool SetSendParameters(const AudioSendParameters& params) override;
  bool SetRecvParameters(const AudioRecvParameters& params) override;
  webrtc::RtpParameters GetRtpSendParameters(uint32_t ssrc) const override;
  webrtc::RTCError SetRtpSendParameters(
      uint32_t ssrc,
      const webrtc::RtpParameters& parameters) override;
  webrtc::RtpParameters GetRtpReceiveParameters(uint32_t ssrc) const override;
  bool SetRtpReceiveParameters(
      uint32_t ssrc,
      const webrtc::RtpParameters& parameters) override;

  void SetPlayout(bool playout) override;
  void SetSend(bool send) override;
  bool SetAudioSend(uint32_t ssrc,
                    bool enable,
                    const AudioOptions* options,
                    AudioSource* source) override;
  bool AddSendStream(const StreamParams& sp) override;
  bool RemoveSendStream(uint32_t ssrc) override;
  bool AddRecvStream(const StreamParams& sp) override;
  bool RemoveRecvStream(uint32_t ssrc) override;

  // E2EE Frame API
  // Set a frame decryptor to a particular ssrc that will intercept all
  // incoming audio payloads and attempt to decrypt them before forwarding the
  // result.
  void SetFrameDecryptor(uint32_t ssrc,
                         rtc::scoped_refptr<webrtc::FrameDecryptorInterface>
                             frame_decryptor) override;
  // Set a frame encryptor to a particular ssrc that will intercept all
  // outgoing audio payloads frames and attempt to encrypt them and forward the
  // result to the packetizer.
  void SetFrameEncryptor(uint32_t ssrc,
                         rtc::scoped_refptr<webrtc::FrameEncryptorInterface>
                             frame_encryptor) override;

  // SSRC=0 will apply the new volume to current and future unsignaled streams.
  bool SetOutputVolume(uint32_t ssrc, double volume) override;

  bool CanInsertDtmf() override;
  bool InsertDtmf(uint32_t ssrc, int event, int duration) override;

  void OnPacketReceived(rtc::CopyOnWriteBuffer* packet,
                        int64_t packet_time_us) override;
  void OnRtcpReceived(rtc::CopyOnWriteBuffer* packet,
                      int64_t packet_time_us) override;
  void OnNetworkRouteChanged(const std::string& transport_name,
                             const rtc::NetworkRoute& network_route) override;
  void OnReadyToSend(bool ready) override;
  bool GetStats(VoiceMediaInfo* info) override;

  // SSRC=0 will set the audio sink on the latest unsignaled stream, future or
  // current. Only one stream at a time will use the sink.
  void SetRawAudioSink(
      uint32_t ssrc,
      std::unique_ptr<webrtc::AudioSinkInterface> sink) override;

  std::vector<webrtc::RtpSource> GetSources(uint32_t ssrc) const override;

  // implements Transport interface
  bool SendRtp(const uint8_t* data,
               size_t len,
               const webrtc::PacketOptions& options) override {
    rtc::CopyOnWriteBuffer packet(data, len, kMaxRtpPacketLen);
    rtc::PacketOptions rtc_options;
    rtc_options.packet_id = options.packet_id;
    if (DscpEnabled()) {
      rtc_options.dscp = PreferredDscp();
    }
    rtc_options.info_signaled_after_sent.included_in_feedback =
        options.included_in_feedback;
    rtc_options.info_signaled_after_sent.included_in_allocation =
        options.included_in_allocation;
    return VoiceMediaChannel::SendPacket(&packet, rtc_options);
  }

  bool SendRtcp(const uint8_t* data, size_t len) override {
    rtc::CopyOnWriteBuffer packet(data, len, kMaxRtpPacketLen);
    rtc::PacketOptions rtc_options;
    if (DscpEnabled()) {
      rtc_options.dscp = PreferredDscp();
    }

    return VoiceMediaChannel::SendRtcp(&packet, rtc_options);
  }

 private:
  bool SetOptions(const AudioOptions& options);
  bool SetRecvCodecs(const std::vector<AudioCodec>& codecs);
  bool SetSendCodecs(const std::vector<AudioCodec>& codecs);
  bool SetLocalSource(uint32_t ssrc, AudioSource* source);
  bool MuteStream(uint32_t ssrc, bool mute);

  WebRtcVoiceEngine* engine() { return engine_; }
  void ChangePlayout(bool playout);
  int CreateVoEChannel();
  bool DeleteVoEChannel(int channel);
  bool SetMaxSendBitrate(int bps);
  void SetupRecording();
  // Check if 'ssrc' is an unsignaled stream, and if so mark it as not being
  // unsignaled anymore (i.e. it is now removed, or signaled), and return true.
  bool MaybeDeregisterUnsignaledRecvStream(uint32_t ssrc);

  rtc::ThreadChecker worker_thread_checker_;

  WebRtcVoiceEngine* const engine_ = nullptr;
  std::vector<AudioCodec> send_codecs_;

  // TODO(kwiberg): decoder_map_ and recv_codecs_ store the exact same
  // information, in slightly different formats. Eliminate recv_codecs_.
  std::map<int, webrtc::SdpAudioFormat> decoder_map_;
  std::vector<AudioCodec> recv_codecs_;

  int max_send_bitrate_bps_ = 0;
  rtc::DiffServCodePoint preferred_dscp_ = rtc::DSCP_DEFAULT;
  AudioOptions options_;
  absl::optional<int> dtmf_payload_type_;
  int dtmf_payload_freq_ = -1;
  bool recv_transport_cc_enabled_ = false;
  bool recv_nack_enabled_ = false;
  bool desired_playout_ = false;
  bool playout_ = false;
  bool send_ = false;
  webrtc::Call* const call_ = nullptr;

  const MediaConfig::Audio audio_config_;

  // Queue of unsignaled SSRCs; oldest at the beginning.
  std::vector<uint32_t> unsignaled_recv_ssrcs_;

  // This is a stream param that comes from the remote description, but wasn't
  // signaled with any a=ssrc lines. It holds the information that was signaled
  // before the unsignaled receive stream is created when the first packet is
  // received.
  StreamParams unsignaled_stream_params_;

  // Volume for unsignaled streams, which may be set before the stream exists.
  double default_recv_volume_ = 1.0;
  // Sink for latest unsignaled stream - may be set before the stream exists.
  std::unique_ptr<webrtc::AudioSinkInterface> default_sink_;
  // Default SSRC to use for RTCP receiver reports in case of no signaled
  // send streams. See: https://code.google.com/p/webrtc/issues/detail?id=4740
  // and https://code.google.com/p/chromium/issues/detail?id=547661
  uint32_t receiver_reports_ssrc_ = 0xFA17FA17u;

  class WebRtcAudioSendStream;
  std::map<uint32_t, WebRtcAudioSendStream*> send_streams_;
  std::vector<webrtc::RtpExtension> send_rtp_extensions_;
  std::string mid_;

  class WebRtcAudioReceiveStream;
  std::map<uint32_t, WebRtcAudioReceiveStream*> recv_streams_;
  std::vector<webrtc::RtpExtension> recv_rtp_extensions_;

  absl::optional<webrtc::AudioSendStream::Config::SendCodecSpec>
      send_codec_spec_;

  // TODO(kwiberg): Per-SSRC codec pair IDs?
  const webrtc::AudioCodecPairId codec_pair_id_ =
      webrtc::AudioCodecPairId::Create();

  // Per peer connection crypto options that last for the lifetime of the peer
  // connection.
  const webrtc::CryptoOptions crypto_options_;
  // Unsignaled streams have an option to have a frame decryptor set on them.
  rtc::scoped_refptr<webrtc::FrameDecryptorInterface>
      unsignaled_frame_decryptor_;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(WebRtcVoiceMediaChannel);
};
}  // namespace cricket

#endif  // MEDIA_ENGINE_WEBRTC_VOICE_ENGINE_H_
