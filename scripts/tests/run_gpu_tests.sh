#!/bin/bash
## Copyright 2022 Intel Corporation
## SPDX-License-Identifier: Apache-2.0

# to run:  ./run_tests.sh <path to ospray source> [TEST_MPI] [TEST_MULTIDEVICE]

SOURCEDIR=$([[ $1 = /* ]] && echo "$1" || echo "$PWD/${1#./}")

if [ -z "$MPI_ROOT_CONFIG" ]; then
  MPI_ROOT_CONFIG="-np 1"
fi
if [ -z "$MPI_WORKER_CONFIG" ]; then
  MPI_WORKER_CONFIG="-np 2"
fi

# optional command line arguments
while [[ $# -gt 0 ]]
do
key="$1"
case $key in
    TEST_MPI)
    TEST_MPI=true
    shift
    ;;
    TEST_MULTIDEVICE)
    TEST_MULTIDEVICE=true
    shift
    ;;
    *)
    shift
    ;;
esac
done

mkdir build_regression_tests
cd build_regression_tests

exitCode=0

cmake -D OSPRAY_TEST_ISA=AVX512SKX "${SOURCEDIR}/test_image_data"
make -j 4 ospray_test_data
let exitCode+=$?

### Excluded tests on GPU
#########################
# Clipping unsupported
test_filters="ClippingParallel.planes"
test_filters+=":TestScenesClipping/FromOsprayTesting.*"
test_filters+=":TestScenesMaxDepth/FromOsprayTestingMaxDepth.test_scenes/1"
test_filters+=":TestScenesMaxDepth/FromOsprayTestingMaxDepth.test_scenes/2"
# Subdivision surfaces unsupported
test_filters+=":TestScenesGeometry/FromOsprayTesting.test_scenes/15"
test_filters+=":TestScenesGeometry/FromOsprayTesting.test_scenes/16"
test_filters+=":TestScenesGeometry/FromOsprayTesting.test_scenes/17"
test_filters+=":Color/Interpolation.Interpolation/4"
test_filters+=":Color/Interpolation.Interpolation/5"
test_filters+=":Color/Interpolation.Interpolation/6"
test_filters+=":Color/Interpolation.Interpolation/7"
test_filters+=":Texcoord/Interpolation.Interpolation/2"
test_filters+=":Texcoord/Interpolation.Interpolation/3"
# Multiple volumes unsupported
test_filters+=":TestScenesVolumes/FromOsprayTesting.test_scenes/3"
test_filters+=":TestScenesVolumes/FromOsprayTesting.test_scenes/4"
test_filters+=":TestScenesVolumes/FromOsprayTesting.test_scenes/5"
test_filters+=":TestScenesVolumesStrictParams/FromOsprayTesting.*"
# Requires non-overlapping multiple volume support on GPU
test_filters+=":ObjectInstance/IDBuffer.*"
# Motion blur unsupported
test_filters+=":TestMotionBlur/MotionBlurBoxes.*"
test_filters+=":CameraRollingShutter/MotionBlurBoxes.*"
test_filters+=":CameraStereoRollingShutter/MotionBlurBoxes.*"
test_filters+=":Camera/MotionCamera.*"
test_filters+=":CameraOrtho/MotionCamera.*"
test_filters+=":CameraStereoRollingShutter/MotionCamera.*"
test_filters+=":LightMotionBlur/*"
# Instancing test also makes use of motion blur
test_filters+=":TestScenesGeometry/FromOsprayTesting.test_scenes/24"
test_filters+=":TestScenesGeometry/FromOsprayTesting.test_scenes/25"
test_filters+=":TestScenesGeometry/FromOsprayTesting.test_scenes/26"
# Variance termination is not quite right
test_filters+=":TestScenesVariance/FromOsprayTestingVariance.testScenes/0"
# 'mix' material not supported on GPU (difficult to implement without fn ptr)
test_filters+=":TestScenesPtMaterials/FromOsprayTesting.test_scenes/13"
# Crashing FIXME
test_filters+=":Primitive/IDBuffer.*"

# Different noise
test_filters+=":TestScenesVolumes/FromOsprayTesting.test_scenes/1"

## Linux only (driver?)

# Artifacts
test_filters+=":TestScenesPtMaterials/FromOsprayTesting.test_scenes/12"
test_filters+=":Renderers/TextureVolumeTransform.simple/0"
test_filters+=":Appearance/Texture2D.filter/*"

# Artifacts on PVC only (DG2 is fine)
test_filters+=":Texcoord/Interpolation.Interpolation/0"
test_filters+=":Texcoord/Interpolation.Interpolation/1"
test_filters+=":Appearance/Texture2DTransform.simple/0"


export ONEAPI_DEVICE_SELECTOR=level_zero:*
export SYCL_CACHE_PERSISTENT=1
export OIDN_VERBOSE=2

mkdir failed-gpu

ospTestSuite --gtest_output=xml:tests.xml --baseline-dir=regression_test_baseline/ --failed-dir=failed-gpu --osp:load-modules=gpu --osp:device=gpu --gtest_filter="-$test_filters" --own-SYCL
let exitCode+=$?

OSPRAY_ALLOW_DEVICE_MEMORY=1 ospTestSuite --baseline-dir=regression_test_baseline/ --failed-dir=failed-gpu --osp:load-modules=gpu --osp:device=gpu --gtest_filter=SharedData/TestUSMSharing.structured_regular/2 --own-SYCL
let exitCode+=$?

if [ $TEST_MULTIDEVICE ]; then
  mkdir failed-multidevice
  # post-processing not enabled on multidevice
  test_filters_md=$test_filters
  test_filters_md+=":DenoiserOp.DenoiserOp"
  test_filters_md+=":DebugOp/ImageOp.ImageOp/0"
  OSPRAY_NUM_SUBDEVICES=2 ospTestSuite --gtest_output=xml:tests.xml --baseline-dir=regression_test_baseline/ --failed-dir=failed-multidevice --gtest_filter="-$test_filters_md" --osp:load-modules=multidevice_gpu --osp:device=multidevice --own-SYCL
  let exitCode+=$?

  OSPRAY_ALLOW_DEVICE_MEMORY=1 OSPRAY_NUM_SUBDEVICES=2 ospTestSuite --gtest_output=xml:tests.xml --baseline-dir=regression_test_baseline/ --failed-dir=failed-multidevice --gtest_filter=SharedData/TestUSMSharing.structured_regular/2 --osp:load-modules=multidevice_gpu --osp:device=multidevice --own-SYCL
  let exitCode+=$?
fi

if [ $TEST_MPI ]; then
  mkdir failed-mpi-gpu
  # Need to export, not just set for MPI to pick it up
  export OSPRAY_MPI_DISTRIBUTED_GPU=1
  mpiexec $MPI_ROOT_CONFIG ospTestSuite --gtest_output=xml:tests-mpi-offload.xml --baseline-dir=regression_test_baseline/ --failed-dir=failed-mpi-gpu --osp:load-modules=mpi_offload --osp:device=mpiOffload --gtest_filter="-$test_filters" : $MPI_WORKER_CONFIG ospray_mpi_worker
  let exitCode+=$?

  mkdir failed-mpi-gpu-data-parallel
  test_filters="MPIDistribTestScenesVolumes/MPIFromOsprayTesting.test_scenes/1" # FIXME
  mpiexec -np 3 ospMPIDistribTestSuite --gtest_output=xml:tests-mpi-distrib.xml --baseline-dir=regression_test_baseline/ --failed-dir=failed-mpi-gpu-data-parallel --gtest_filter="-$test_filters"
  let exitCode+=$?
fi

exit $exitCode
