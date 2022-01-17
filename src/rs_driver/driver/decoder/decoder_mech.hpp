/*********************************************************************************************************************
Copyright (c) 2020 RoboSense
All rights reserved

By downloading, copying, installing or using the software you agree to this license. If you do not agree to this
license, do not download, install, copy or use the software.

License Agreement
For RoboSense LiDAR SDK Library
(3-clause BSD License)

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the names of the RoboSense, nor Suteng Innovation Technology, nor the names of other contributors may be used
to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*********************************************************************************************************************/

#pragma once
#include <rs_driver/driver/decoder/decoder.hpp>

namespace robosense
{
namespace lidar
{

struct RSDecoderMechConstParam
{
  RSDecoderConstParam base;

  // lens center
  float RX;
  float RY;
  float RZ;

  // firing_ts/chan_ts
  double BLOCK_DURATION;
  double CHAN_TSS[128];
  float CHAN_AZIS[128];
};

typedef struct
{
  uint16_t rpm;
  RSFOV fov;
  uint8_t return_mode;
  RSCalibrationAngle vert_angle_cali[32];
  RSCalibrationAngle horiz_angle_cali[32];
} AdapterDifopPkt;

class DecoderMech : public Decoder
{
public:

  constexpr static int32_t RS_ONE_ROUND = 36000;

  virtual ~DecoderMech() = default;

  explicit DecoderMech(const RSDecoderMechConstParam& const_param, 
      const RSDecoderParam& param, 
      const std::function<void(const Error&)>& excb);

  void print();

#ifndef UNIT_TEST
protected:
#endif

  template <typename T_Difop>
  void decodeDifopCommon(const T_Difop& pkt);

  RSDecoderMechConstParam mech_const_param_; // const param 
  ChanAngles chan_angles_; // vert_angles/horiz_angles adjustment
  AzimuthSection scan_section_; // valid azimuth section
  std::shared_ptr<SplitStrategy> split_strategy_; // split strategy

  uint16_t rps_; // rounds per second
  uint16_t blks_per_frame_; // blocks per frame/round
  uint16_t split_blks_per_frame_; // blocks in msop pkt per frame/round. 
  uint16_t block_azi_diff_; // azimuth difference between adjacent blocks.
  float fov_blind_ts_diff_; // timestamp difference across blind section(defined by fov)

  int lidar_alph0_;  // lens center related
  float lidar_Rxy_;  // lens center related
};

inline DecoderMech::DecoderMech(const RSDecoderMechConstParam& const_param, 
    const RSDecoderParam& param, 
    const std::function<void(const Error&)>& excb)
  : Decoder(const_param.base, param, excb)
  , mech_const_param_(const_param)
  , chan_angles_(this->const_param_.CHANNELS_PER_BLOCK)
  , scan_section_(this->param_.start_angle * 100, this->param_.end_angle * 100)
  , rps_(10)
  , blks_per_frame_(1/(10*this->mech_const_param_.BLOCK_DURATION))
  , split_blks_per_frame_(blks_per_frame_)
  , block_azi_diff_(20)
  , fov_blind_ts_diff_(0)
{
  this->height_ = this->const_param_.CHANNELS_PER_BLOCK;
  this->packet_duration_ = 
    this->mech_const_param_.BLOCK_DURATION * this->const_param_.BLOCKS_PER_PKT;

  switch (this->param_.split_frame_mode)
  {
    case SplitFrameMode::SPLIT_BY_FIXED_BLKS:
      split_strategy_ = std::make_shared<SplitStrategyByNum>(&this->split_blks_per_frame_);
      break;

    case SplitFrameMode::SPLIT_BY_CUSTOM_BLKS:
      split_strategy_ = std::make_shared<SplitStrategyByNum>(&this->param_.num_blks_split);
      break;

    case SplitFrameMode::SPLIT_BY_ANGLE:
    default:
      uint16_t angle = (uint16_t)(this->param_.split_angle * 100);
      split_strategy_ = std::make_shared<SplitStrategyByAngle>(angle);
      break;
  }

  // lens center: (alph0, Rxy)
  lidar_alph0_ = std::atan2(mech_const_param_.RY, mech_const_param_.RX) * 180 / M_PI * 100;
  lidar_Rxy_ = std::sqrt(mech_const_param_.RX * mech_const_param_.RX + 
      mech_const_param_.RY * mech_const_param_.RY);

  if (this->param_.config_from_file)
  {
    int ret = chan_angles_.loadFromFile(this->param_.angle_path);
    this->angles_ready_ = (ret == 0);

    if (this->param_.wait_for_difop)
    {
      this->param_.wait_for_difop = false;

      RS_WARNING << "wait_for_difop cannot be true when config_from_file is true."
                 << " reset it to be false." << RS_REND;
    }
  }
}

inline void DecoderMech::print()
{
  std::cout << "-----------------------------------------" << std::endl
    << "rps:\t\t\t" << this->rps_ << std::endl
    << "echo_mode:\t\t" << this->echo_mode_ << std::endl
    << "blks_per_frame:\t\t" << this->blks_per_frame_ << std::endl
    << "split_blks_per_frame:\t" << this->split_blks_per_frame_ << std::endl
    << "block_azi_diff:\t\t" << this->block_azi_diff_ << std::endl
    << "fov_blind_ts_diff:\t" << this->fov_blind_ts_diff_ << std::endl
    << "angle_from_file:\t" << this->param_.config_from_file << std::endl
    << "angles_ready:\t\t" << this->angles_ready_ << std::endl;

  this->chan_angles_.print();
}

template <typename T_Difop>
inline void DecoderMech::decodeDifopCommon(const T_Difop& pkt)
{
  // rounds per second
  this->rps_ = ntohs(pkt.rpm) / 60;
  if (this->rps_ == 0)
  {
    RS_WARNING << "LiDAR RPM is 0. Use default value 600." << RS_REND;
    this->rps_ = 10;
  }

  // blocks per frame
  this->blks_per_frame_ = 1 / (this->rps_ * this->mech_const_param_.BLOCK_DURATION);

  // block diff of azimuth
  this->block_azi_diff_ = 
    std::round(RS_ONE_ROUND * this->rps_ * this->mech_const_param_.BLOCK_DURATION);

  // fov related
  uint16_t fov_start_angle = ntohs(pkt.fov.start_angle);
  uint16_t fov_end_angle = ntohs(pkt.fov.end_angle);
  uint16_t fov_range = (fov_start_angle < fov_end_angle) ? 
    (fov_end_angle - fov_start_angle) : (fov_end_angle + RS_ONE_ROUND - fov_start_angle);
  uint16_t fov_blind_range = RS_ONE_ROUND - fov_range;

  // fov blind diff of timestamp
  this->fov_blind_ts_diff_ = 
    (float)fov_blind_range / ((float)RS_ONE_ROUND * (float)this->rps_);

  if (!this->param_.config_from_file && !this->angles_ready_)
  {
    int ret = this->chan_angles_.loadFromDifop(pkt.vert_angle_cali, pkt.horiz_angle_cali, 
        this->const_param_.CHANNELS_PER_BLOCK);
    this->angles_ready_ = (ret == 0);
  }
}

}  // namespace lidar
}  // namespace robosense
