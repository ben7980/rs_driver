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
PECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*********************************************************************************************************************/

#include <rs_driver/driver/decoder/decoder.hpp>
namespace robosense
{
namespace lidar
{
#pragma pack(push, 1)
typedef struct
{
  uint8_t id[2];
  uint16_t azimuth;
  RSChannel channels[32];
} RSHELIOSMsopBlock;

typedef struct
{
  uint8_t id[4];
  uint16_t protocol_version;
  uint8_t reserved1[14];
  RSTimestampUTC timestamp;
  uint8_t lidar_type;
  uint8_t reserved2[7];
  RSTemperature temp;
  uint8_t reserved3[2];
} RSHELIOSMsopHeader;

typedef struct
{
  RSHELIOSMsopHeader header;
  RSHELIOSMsopBlock blocks[12];
  unsigned int index;
  uint16_t tail;
} RSHELIOSMsopPkt;

typedef struct
{
  uint8_t id[8];
  uint16_t rpm;
  RSEthNetV2 eth;
  RSFOV fov;
  uint8_t reserved1[2];
  uint16_t phase_lock_angle;
  RSVersionV2 version;
  uint8_t reserved2[229];
  RSSN sn;
  uint16_t zero_cali;
  uint8_t return_mode;
  RSTimeInfo time_info;
  RSStatusV2 status;
  uint8_t reserved3[5];
  RSDiagnoV2 diagno;
  uint8_t gprmc[86];
  RSCalibrationAngle ver_angle_cali[32];
  RSCalibrationAngle hori_angle_cali[32];
  uint8_t reserved4[586];
  uint16_t tail;
} RSHELIOSDifopPkt;

#pragma pack(pop)

template <typename T_PointCloud>
class DecoderRSHELIOS : public Decoder<T_PointCloud>
{
public:
  virtual void decodeDifopPkt(const uint8_t* pkt, size_t size);
  virtual void decodeMsopPkt(const uint8_t* pkt, size_t size);
  virtual ~DecoderRSHELIOS() = default;

  explicit DecoderRSHELIOS(const RSDecoderParam& param, 
      const std::function<void(const Error&)>& excb);

#ifndef UNIT_TEST
protected:
#endif

  static RSDecoderConstParam getConstParam();
  static RSEchoMode getEchoMode(uint8_t mode);

  template <typename T_BlockDiff>
  void internDecodeMsopPkt(const uint8_t* pkt, size_t size);
};

#if 0
template <typename T_PointCloud>
inline DecoderRSHELIOS<T_PointCloud>::DecoderRSHELIOS(const RSDecoderParam& param,
                                                      const LidarConstantParameter& lidar_const_param)
  : DecoderBase<T_PointCloud>(param, lidar_const_param)
{
  this->vert_angle_list_.resize(this->lidar_const_param_.LASER_NUM);
  this->hori_angle_list_.resize(this->lidar_const_param_.LASER_NUM);
  this->beam_ring_table_.resize(this->lidar_const_param_.LASER_NUM);
  if (this->param_.max_distance > 100.0f)
  {
    this->param_.max_distance = 100.0f;
  }
  if (this->param_.min_distance < 0.1f || this->param_.min_distance > this->param_.max_distance)
  {
    this->param_.min_distance = 0.1f;
  }
}

template <typename T_PointCloud>
inline double DecoderRSHELIOS<T_PointCloud>::getLidarTime(const uint8_t* pkt)
{
  return this->template calculateTimeUTC<RSHELIOSMsopPkt>(pkt, LidarType::RSHELIOS);
}

template <typename T_PointCloud>
inline RSDecoderResult DecoderRSHELIOS<T_PointCloud>::decodeMsopPkt(const uint8_t* pkt,
                                                                    typename T_PointCloud::VectorT& vec, int& height,
                                                                    int& azimuth)
{
  height = this->lidar_const_param_.LASER_NUM;
  const RSHELIOSMsopPkt* mpkt_ptr = reinterpret_cast<const RSHELIOSMsopPkt*>(pkt);
  if (mpkt_ptr->header.id != this->lidar_const_param_.MSOP_ID)
  {
    return RSDecoderResult::WRONG_PKT_HEADER;
  }
  this->protocol_ver_ = RS_SWAP_SHORT(mpkt_ptr->header.protocol_version);
  azimuth = RS_SWAP_SHORT(mpkt_ptr->blocks[0].azimuth);
  this->current_temperature_ = this->computeTemperature(mpkt_ptr->header.temp_raw);
  double block_timestamp = this->get_point_time_func_(pkt);
  float azi_diff = 0;
  for (size_t blk_idx = 0; blk_idx < this->lidar_const_param_.BLOCKS_PER_PKT; blk_idx++)
  {
    if (mpkt_ptr->blocks[blk_idx].id != this->lidar_const_param_.BLOCK_ID)
    {
      break;
    }
    int cur_azi = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx].azimuth);
    if (this->echo_mode_ == ECHO_DUAL)
    {
      if (blk_idx % 2 == 0)
      {
        if (blk_idx == 0)
        {
          azi_diff = static_cast<float>(
              (RS_ONE_ROUND + RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx + 2].azimuth) - cur_azi) % RS_ONE_ROUND);
        }
        else
        {
          azi_diff = static_cast<float>(
              (RS_ONE_ROUND + cur_azi - RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx - 2].azimuth)) % RS_ONE_ROUND);
          block_timestamp = (azi_diff > 100) ? (block_timestamp + this->fov_time_jump_diff_) :
                                               (block_timestamp + this->time_duration_between_blocks_);
        }
      }
    }
    else
    {
      if (blk_idx == 0)  // 12
      {
        azi_diff = static_cast<float>((RS_ONE_ROUND + RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx + 1].azimuth) - cur_azi) %
                                      RS_ONE_ROUND);
      }
      else
      {
        azi_diff = static_cast<float>((RS_ONE_ROUND + cur_azi - RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx - 1].azimuth)) %
                                      RS_ONE_ROUND);
        block_timestamp = (azi_diff > 100) ? (block_timestamp + this->fov_time_jump_diff_) :
                                             (block_timestamp + this->time_duration_between_blocks_);
      }
    }
    azi_diff = (azi_diff > 100) ? this->azi_diff_between_block_theoretical_ : azi_diff;
    for (int channel_idx = 0; channel_idx < this->lidar_const_param_.CHANNELS_PER_BLOCK; channel_idx++)
    {
      float azi_channel_ori =
          cur_azi +
          azi_diff * this->lidar_const_param_.DSR_TOFFSET * this->lidar_const_param_.FIRING_FREQUENCY *
              ((-0.014f * static_cast<float>(channel_idx) + 1.8965f) * static_cast<float>(channel_idx) - 0.6543f);
      int azi_channel_final = this->azimuthCalibration(azi_channel_ori, channel_idx);
      float distance = RS_SWAP_SHORT(mpkt_ptr->blocks[blk_idx].channels[channel_idx].distance) *
                       this->lidar_const_param_.DIS_RESOLUTION;
      int angle_horiz = (int)(azi_channel_ori + RS_ONE_ROUND) % RS_ONE_ROUND;
      int angle_vert = ((this->vert_angle_list_[channel_idx]) + RS_ONE_ROUND) % RS_ONE_ROUND;

      // store to point cloud buffer
      typename T_PointCloud::PointT point;
      bool pointValid = false;
      if ((distance <= this->param_.max_distance && distance >= this->param_.min_distance) &&
          ((this->angle_flag_ && azi_channel_final >= this->start_angle_ && azi_channel_final <= this->end_angle_) ||
           (!this->angle_flag_ &&
            ((azi_channel_final >= this->start_angle_) || (azi_channel_final <= this->end_angle_)))))
      {
        float x = distance * this->checkCosTable(angle_vert) * this->checkCosTable(azi_channel_final) +
                  this->lidar_const_param_.RX * this->checkCosTable(angle_horiz);
        float y = -distance * this->checkCosTable(angle_vert) * this->checkSinTable(azi_channel_final) -
                  this->lidar_const_param_.RX * this->checkSinTable(angle_horiz);
        float z = distance * this->checkSinTable(angle_vert) + this->lidar_const_param_.RZ;
        uint8_t intensity = mpkt_ptr->blocks[blk_idx].channels[channel_idx].intensity;
        this->transformPoint(x, y, z);
        setX(point, x);
        setY(point, y);
        setZ(point, z);
        setIntensity(point, intensity);
        pointValid = true;
      }
      else if (!this->param_.is_dense)
      {
        setX(point, NAN);
        setY(point, NAN);
        setZ(point, NAN);
        setIntensity(point, 0);
        pointValid = true;
      }

      if (pointValid)
      {
        setRing(point, this->beam_ring_table_[channel_idx]);
        setTimestamp(point, block_timestamp);
        vec.emplace_back(std::move(point));
      }
    }
  }
  return RSDecoderResult::DECODE_OK;
}

template <typename T_PointCloud>
inline RSDecoderResult DecoderRSHELIOS<T_PointCloud>::decodeDifopPkt(const uint8_t* pkt)
{
  RSHELIOSDifopPkt* dpkt_ptr = (RSHELIOSDifopPkt*)pkt;
  if (dpkt_ptr->id != this->lidar_const_param_.DIFOP_ID)
  {
    return RSDecoderResult::WRONG_PKT_HEADER;
  }
  this->template decodeDifopCommon<RSHELIOSDifopPkt>(pkt, LidarType::RSHELIOS);
  if (!this->difop_flag_)
  {
    this->template decodeDifopCalibration<RSHELIOSDifopPkt>(pkt, LidarType::RSHELIOS);
  }
  return RSDecoderResult::DECODE_OK;
}
#endif

}  // namespace lidar
}  // namespace robosense
