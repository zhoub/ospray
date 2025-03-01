// Copyright 2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "SparseFB.h"
#include "OSPConfig.h"
#include "PixelOp.h"
#include "fb/FrameBufferView.h"
#include "render/util.h"
#include "rkcommon/common.h"
#include "rkcommon/tasking/parallel_for.h"
#include "rkcommon/tracing/Tracing.h"
#include "rkcommon/utility/ArrayView.h"
#ifndef OSPRAY_TARGET_SYCL
#include "fb/SparseFB_ispc.h"
#endif

#include <cstdlib>
#include <numeric>

namespace ospray {

// A global function so we can call it from the tile initialization SYCL kernel
box2i getTileRegion(uint32_t tileID, const vec2i fbSize, const vec2i totalTiles)
{
  const vec2i tilePos(tileID % totalTiles.x, tileID / totalTiles.x);
  return box2i(
      tilePos * TILE_SIZE, min(tilePos * TILE_SIZE + TILE_SIZE, fbSize));
}

SparseFrameBuffer::SparseFrameBuffer(api::ISPCDevice &device,
    const vec2i &_size,
    ColorBufferFormat _colorBufferFormat,
    const uint32 channels,
    const std::vector<uint32_t> &_tileIDs,
    const bool overrideUseTaskAccumIDs)
    : AddStructShared(device.getIspcrtContext(),
        device,
        _size,
        _colorBufferFormat,
        channels,
        FFO_FB_SPARSE),
      device(device),
      useTaskAccumIDs((channels & OSP_FB_ACCUM) || overrideUseTaskAccumIDs),
      totalTiles(divRoundUp(size, vec2i(TILE_SIZE)))
{
  if (size.x <= 0 || size.y <= 0) {
    throw std::runtime_error(
        "local framebuffer has invalid size. Dimensions must be greater than "
        "0");
  }

  setTiles(_tileIDs);
}

SparseFrameBuffer::SparseFrameBuffer(api::ISPCDevice &device,
    const vec2i &_size,
    ColorBufferFormat _colorBufferFormat,
    const uint32 channels,
    const bool overrideUseTaskAccumIDs)
    : AddStructShared(device.getIspcrtContext(),
        device,
        _size,
        _colorBufferFormat,
        channels,
        FFO_FB_SPARSE),
      device(device),
      useTaskAccumIDs((channels & OSP_FB_ACCUM) || overrideUseTaskAccumIDs),
      totalTiles(divRoundUp(size, vec2i(TILE_SIZE)))
{
  if (size.x <= 0 || size.y <= 0) {
    throw std::runtime_error(
        "local framebuffer has invalid size. Dimensions must be greater than "
        "0");
  }
}

void SparseFrameBuffer::commit()
{
  FrameBuffer::commit();

  if (imageOpData) {
    FrameBufferView fbv(this,
        getColorBufferFormat(),
        getNumPixels(),
        nullptr,
        nullptr,
        nullptr,
        nullptr);

    // Sparse framebuffer cannot execute frame operations because it doesn't
    // have the full framebuffer. This is handled by the parent object managing
    // the set of sparse framebuffer's, so here we just ignore them
    prepareLiveOpsForFBV(fbv, false, true);
  }
}

vec2i SparseFrameBuffer::getNumRenderTasks() const
{
  return numRenderTasks;
}

uint32_t SparseFrameBuffer::getTotalRenderTasks() const
{
  return numRenderTasks.product();
}

utility::ArrayView<uint32_t> SparseFrameBuffer::getRenderTaskIDs(
    float errorThreshold)
{
  if (!renderTaskIDs)
    return utility::ArrayView<uint32_t>(nullptr, 0);

  if (errorThreshold > 0.0f && hasVarianceBuffer) {
    RKCOMMON_IF_TRACING_ENABLED(
        rkcommon::tracing::beginEvent("buildActiveTaskIDs", "SparseFb"));

    auto last = std::copy_if(renderTaskIDs->begin(),
        renderTaskIDs->end(),
        activeTaskIDs->begin(),
        [=](uint32_t i) { return taskError(i) > errorThreshold; });

    const size_t numActive = last - activeTaskIDs->begin();
    ispcrt::TaskQueue &tq = device.getIspcrtQueue();
    tq.copyToDevice(*activeTaskIDs);
    tq.sync();

    RKCOMMON_IF_TRACING_ENABLED(rkcommon::tracing::endEvent());

    return utility::ArrayView<uint32_t>(activeTaskIDs->devicePtr(), numActive);
  } else {
    RKCOMMON_IF_TRACING_ENABLED(
        rkcommon::tracing::setMarker("returnAllTaskIDs", "SparseFB"));
    return utility::ArrayView<uint32_t>(
        renderTaskIDs->devicePtr(), renderTaskIDs->size());
  }
}

std::string SparseFrameBuffer::toString() const
{
  return "ospray::SparseFrameBuffer";
}

float SparseFrameBuffer::taskError(const uint32_t taskID) const
{
  //  If this SparseFB doesn't have any tiles return 0. This should not
  //  typically be called in this case anyways
  if (!tiles) {
    return 0.f;
  }

  if (!taskErrorBuffer) {
    throw std::runtime_error(
        "SparseFrameBuffer::taskError: trying to get task error on FB without variance/error buffers");
  }
  // TODO: Should sync taskError back in endFrame
  return (*taskErrorBuffer)[taskID];
}

void SparseFrameBuffer::setTaskError(const uint32_t taskID, const float error)
{
  //  If this SparseFB doesn't have any tiles then do nothing. This should not
  //  typically be called in this case anyways
  if (!tiles) {
    return;
  }
  if (!taskErrorBuffer) {
    throw std::runtime_error(
        "SparseFrameBuffer::setTaskError: trying to set task error on FB without variance/error buffers");
  }
  // TODO: dirty tracking for task error, sync in begin frame
  (*taskErrorBuffer)[taskID] = error;
}

void SparseFrameBuffer::beginFrame()
{
  FrameBuffer::beginFrame();

  if (tiles) {
#ifndef OSPRAY_TARGET_SYCL
    for (auto &tile : *tiles) {
      tile.accumID = getFrameID();
    }
#else
    const size_t numTasks = tiles->size();
    auto *fbSh = getSh();
    const int32 frameID = getFrameID();
    device.getSyclQueue()
        .submit([&](sycl::handler &cgh) {
          const sycl::nd_range<1> dispatchRange =
              device.computeDispatchRange(numTasks, 16);
          cgh.parallel_for(dispatchRange, [=](sycl::nd_item<1> taskIndex) {
            if (taskIndex.get_global_id(0) < numTasks) {
              fbSh->tiles[taskIndex.get_global_id(0)].accumID = frameID;
            }
          });
        })
        .wait_and_throw();
#endif
  }
  tilesDirty = true;
}

const void *SparseFrameBuffer::mapBuffer(OSPFrameBufferChannel)
{
  return nullptr;
}

void SparseFrameBuffer::unmap(const void *) {}

void SparseFrameBuffer::clear()
{
  FrameBuffer::clear();

  // We only need to reset the accumID, SparseFB_accumulateWriteSample will
  // handle overwriting the image when accumID == 0
  if (taskAccumID) {
    std::fill(taskAccumID->begin(), taskAccumID->end(), 0);
    ispcrt::TaskQueue &tq = device.getIspcrtQueue();
    tq.copyToDevice(*taskAccumID);
    tq.sync();
  }

  // also clear the task error buffer if present
  if (taskErrorBuffer) {
    std::fill(taskErrorBuffer->begin(), taskErrorBuffer->end(), inf);
  }
}

size_t SparseFrameBuffer::getNumTiles() const
{
  return tiles ? tiles->size() : 0;
}

const utility::ArrayView<Tile> SparseFrameBuffer::getTiles()
{
  if (!tiles) {
    return utility::ArrayView<Tile>(nullptr, 0);
  }
  if (tilesDirty) {
    RKCOMMON_IF_TRACING_ENABLED(
        rkcommon::tracing::beginEvent("SparseFB::getTiles", "ospray"));

    tilesDirty = false;
    ispcrt::TaskQueue &tq = device.getIspcrtQueue();
    tq.copyToHost(*tiles);
    tq.sync();

    RKCOMMON_IF_TRACING_ENABLED(rkcommon::tracing::endEvent());
  }

  return utility::ArrayView<Tile>(tiles->hostPtr(), tiles->size());
}

const utility::ArrayView<Tile> SparseFrameBuffer::getTilesDevice() const
{
  if (!tiles) {
    return utility::ArrayView<Tile>(nullptr, 0);
  }

  return utility::ArrayView<Tile>(tiles->devicePtr(), tiles->size());
}

const utility::ArrayView<uint32_t> SparseFrameBuffer::getTileIDs()
{
  if (tileIDs.empty()) {
    return utility::ArrayView<uint32_t>(nullptr, 0);
  }
  return utility::ArrayView<uint32_t>(tileIDs.data(), tileIDs.size());
}

uint32_t SparseFrameBuffer::getTileIndexForTask(uint32_t taskID) const
{
  // Find which tile this task falls into
  // tileIdx -> index in the SparseFB's list of tiles
  return taskID / getNumTasksPerTile();
}

void SparseFrameBuffer::setTiles(
    const std::vector<uint32_t> &_tileIDs, const int initialTaskAccumID)
{
  RKCOMMON_IF_TRACING_ENABLED({
    rkcommon::tracing::beginEvent("SparseFB::setTiles", "ospray");
    rkcommon::tracing::setCounter("SparseFB::numTiles", _tileIDs.size());
  });
  // (Re-)configure the sparse framebuffer based on the tileIDs we're passed
  tileIDs = _tileIDs;
  numRenderTasks =
      vec2i(tileIDs.size() * TILE_SIZE, TILE_SIZE) / getRenderTaskSize();

  ispcrt::TaskQueue &tq = device.getIspcrtQueue();

  if (hasVarianceBuffer && !tileIDs.empty()) {
    taskErrorBuffer = make_buffer_shared_unique<float>(
        getISPCDevice().getIspcrtContext(), numRenderTasks.long_product());
    std::fill(taskErrorBuffer->begin(), taskErrorBuffer->end(), inf);
  } else {
    taskErrorBuffer = nullptr;
  }

  if (!tileIDs.empty()) {
    tiles = make_buffer_device_shadowed_unique<Tile>(
        getISPCDevice().getIspcrtDevice(), tileIDs.size());
    const vec2f rcpSize = rcp(vec2f(size));
#ifndef OSPRAY_TARGET_SYCL
    rkcommon::tasking::parallel_for(tiles->size(), [&](size_t i) {
      Tile &t = (*tiles)[i];
      t.fbSize = size;
      t.rcp_fbSize = rcpSize;
      t.region = getTileRegion(tileIDs[i]);
      t.accumID = 0;
    });
#else
    BufferDeviceShadowed<uint32_t> deviceTileIDs(
        device.getIspcrtDevice(), tileIDs);
    tq.copyToDevice(deviceTileIDs);
    tq.sync();

    // In SYCL, populate the tiles on the device.
    // TODO: Best to unify the codepaths more here and do the ISPC device
    // tile population in ISPC so it also runs on the "device"
    const uint32_t *deviceTileIDsPtr = deviceTileIDs.devicePtr();
    const size_t numTasks = tiles->size();
    const vec2i fbSize = size;
    const vec2i fbTotalTiles = totalTiles;
    Tile *tilesDevice = tiles->devicePtr();
    device.getSyclQueue()
        .submit([&](sycl::handler &cgh) {
          const sycl::nd_range<1> dispatchRange =
              device.computeDispatchRange(numTasks, 16);
          cgh.parallel_for(dispatchRange, [=](sycl::nd_item<1> taskIndex) {
            if (taskIndex.get_global_id(0) < numTasks) {
              const size_t tid = taskIndex.get_global_id(0);
              tilesDevice[tid].fbSize = fbSize;
              tilesDevice[tid].rcp_fbSize = rcpSize;
              tilesDevice[tid].region = ospray::getTileRegion(
                  deviceTileIDsPtr[tid], fbSize, fbTotalTiles);
              tilesDevice[tid].accumID = 0;
            }
          });
        })
        .wait_and_throw();
#endif
    tilesDirty = true;
  } else {
    tiles = nullptr;
  }

  const size_t numPixels = tiles ? tileIDs.size() * TILE_SIZE * TILE_SIZE : 0;
  if (hasVarianceBuffer && !tileIDs.empty()) {
    varianceBuffer = make_buffer_device_unique<vec4f>(
        getISPCDevice().getIspcrtDevice(), numPixels);
  } else {
    varianceBuffer = nullptr;
  }

  if (hasAccumBuffer && !tileIDs.empty()) {
    accumulationBuffer = make_buffer_device_unique<vec4f>(
        getISPCDevice().getIspcrtDevice(), numPixels);
  } else {
    accumulationBuffer = nullptr;
  }

  if ((hasAccumBuffer || useTaskAccumIDs) && !tileIDs.empty()) {
    taskAccumID = make_buffer_device_shadowed_unique<int>(
        getISPCDevice().getIspcrtDevice(), getTotalRenderTasks());
    std::fill(taskAccumID->begin(), taskAccumID->end(), initialTaskAccumID);
    tq.copyToDevice(*taskAccumID);
  } else {
    taskAccumID = nullptr;
  }

  // TODO: Should find a better way for allowing sparse task id sets
  // here we have this array b/c the tasks will be filtered down based on
  // variance termination
  if (!tileIDs.empty()) {
    renderTaskIDs = make_buffer_device_shadowed_unique<uint32_t>(
        getISPCDevice().getIspcrtDevice(), getTotalRenderTasks());
    std::iota(renderTaskIDs->begin(), renderTaskIDs->end(), 0);
  } else {
    renderTaskIDs = nullptr;
  }
  if (hasVarianceBuffer && !tileIDs.empty()) {
    activeTaskIDs = make_buffer_device_shadowed_unique<uint32_t>(
        getISPCDevice().getIspcrtDevice(), getTotalRenderTasks());
  } else {
    activeTaskIDs = nullptr;
  }

  const uint32_t nTasksPerTile = getNumTasksPerTile();

  // Sort each tile's tasks in Z order
  // TODO: is this worth doing in the dynamicLB case? We make
  // a new sparseFb for each tile set we receive, it seems like
  // this won't be worth it.
  if (tiles) {
#ifndef OSPRAY_TARGET_SYCL
    // We use a 1x1 task size in SYCL and this sorting may not pay off for the
    // cost it adds
    rkcommon::tasking::parallel_for(tiles->size(), [&](const size_t i) {
      std::sort(renderTaskIDs->begin() + i * nTasksPerTile,
          renderTaskIDs->begin() + (i + 1) * nTasksPerTile,
          [&](const uint32_t &a, const uint32_t &b) {
            const vec2i p_a = getTaskPosInTile(a);
            const vec2i p_b = getTaskPosInTile(b);
            return interleaveZOrder(p_a.x, p_a.y)
                < interleaveZOrder(p_b.x, p_b.y);
          });
    });
#endif

    // Upload the task IDs to the device
    tq.copyToDevice(*renderTaskIDs);
  }
  tq.sync();

  // We don't call SparseFB::clear here because we've populated
  // taskAccumID with the initial accumID to use. We just want
  // to reset the frameID
  FrameBuffer::clear();

#ifndef OSPRAY_TARGET_SYCL
  getSh()->super.accumulateSample =
      reinterpret_cast<ispc::FrameBuffer_accumulateSampleFct>(
          ispc::SparseFrameBuffer_accumulateSample_addr());
  getSh()->super.getRenderTaskDesc =
      reinterpret_cast<ispc::FrameBuffer_getRenderTaskDescFct>(
          ispc::SparseFrameBuffer_getRenderTaskDesc_addr());
  getSh()->super.completeTask =
      reinterpret_cast<ispc::FrameBuffer_completeTaskFct>(
          ispc::SparseFrameBuffer_completeTask_addr());
#endif

  getSh()->numRenderTasks = numRenderTasks;
  getSh()->totalTiles = totalTiles;

  getSh()->tiles = tiles ? tiles->devicePtr() : nullptr;
  getSh()->numTiles = tiles ? tiles->size() : 0;

  getSh()->taskAccumID = taskAccumID ? taskAccumID->devicePtr() : nullptr;
  getSh()->accumulationBuffer =
      accumulationBuffer ? accumulationBuffer->devicePtr() : nullptr;
  getSh()->varianceBuffer =
      varianceBuffer ? varianceBuffer->devicePtr() : nullptr;
  getSh()->taskRegionError =
      taskErrorBuffer ? taskErrorBuffer->data() : nullptr;

  RKCOMMON_IF_TRACING_ENABLED(rkcommon::tracing::endEvent());
}

box2i SparseFrameBuffer::getTileRegion(uint32_t tileID) const
{
  return ospray::getTileRegion(tileID, size, totalTiles);
}

vec2i SparseFrameBuffer::getTaskPosInTile(const uint32_t taskID) const
{
  // Find where this task is supposed to render within this tile
  const vec2i tasksPerTile = vec2i(TILE_SIZE) / getRenderTaskSize();
  const uint32 taskTileID = taskID % (tasksPerTile.x * tasksPerTile.y);
  vec2i taskStart(taskTileID % tasksPerTile.x, taskTileID / tasksPerTile.x);
  return taskStart * getRenderTaskSize();
}

uint32_t SparseFrameBuffer::getNumTasksPerTile() const
{
  const vec2i tileDims(TILE_SIZE);
  const vec2i tasksPerTile = tileDims / getRenderTaskSize();
  return tasksPerTile.long_product();
}

} // namespace ospray
