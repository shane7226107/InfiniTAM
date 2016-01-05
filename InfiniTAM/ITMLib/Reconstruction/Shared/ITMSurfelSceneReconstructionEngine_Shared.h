// Copyright 2014-2015 Isis Innovation Limited and the authors of InfiniTAM

#pragma once

#include "../../Objects/Camera/ITMIntrinsics.h"
#include "../../Objects/Scene/ITMRepresentationAccess.h"

namespace ITMLib
{

//#################### HELPERS ####################

/**
 * \brief TODO
 *
 * \param invT  A transformation from global coordinates to pose coordinates.
 * \param p     The point whose depth we want to calculate.
 */
_CPU_AND_GPU_CODE_
inline float calculate_depth_from_pose(const Matrix4f& invT, const Vector3f& p)
{
  Vector4f vg(p.x, p.y, p.z, 1.0f);
  Vector4f v = invT * vg;
  return v.z;
}

/**
 * \brief TODO
 */
_CPU_AND_GPU_CODE_
inline Vector3f transform_point(const Matrix4f& T, const Vector3f& p)
{
  Vector4f v(p.x, p.y, p.z, 1.0f);
  return (T * v).toVector3();
}

//#################### MAIN FUNCTIONS ####################

/**
 * \brief TODO
 */
template <typename TSurfel>
_CPU_AND_GPU_CODE_
inline void add_new_surfel(int locId, const Matrix4f& T, const unsigned short *newPointsMask, const unsigned int *newPointsPrefixSum,
                           const Vector3f *vertexMap, const Vector4f *normalMap, const float *radiusMap, const Vector4u *colourMap,
                           int timestamp, TSurfel *newSurfels, const TSurfel *surfels, const unsigned int *correspondenceMap,
                           int colourMapWidth, int colourMapHeight, const Matrix4f& depthToRGB, const Vector4f& projParamsRGB)
{
  if(newPointsMask[locId])
  {
    const Vector3f v = vertexMap[locId];

    TSurfel surfel;
    surfel.position = transform_point(T, v);
    surfel.normal = normalMap[locId].toVector3();
    surfel.radius = radiusMap[locId];
    surfel.confidence = 1.0f;                     // TEMPORARY
    surfel.timestamp = timestamp;

    // Store a colour if the surfel type can support it.
#if 1
    Vector3f cv = transform_point(depthToRGB, v);
    int cx = static_cast<int>(projParamsRGB.x * cv.x / cv.z + projParamsRGB.z + 0.5f);
    int cy = static_cast<int>(projParamsRGB.y * cv.y / cv.z + projParamsRGB.w + 0.5f);
    Vector3u colour((uchar)0);
    if(cx >= 0 && cx < colourMapWidth && cy >= 0 && cy < colourMapHeight)
    {
      colour = colourMap[cy * colourMapWidth + cx].toVector3();
    }
    SurfelColourManipulator<TSurfel::hasColourInformation>::write(surfel, colour);
#endif
#if 0
    // TEMPORARY: Read from the proper position in the colour map.
    if(colourMapWidth == 320)
    {
        int x = (locId % 640) / 2;
        int y = (locId / 640) / 2;
        SurfelColourManipulator<TSurfel::hasColourInformation>::write(surfel, colourMap[y * 320 + x].toVector3());
    }
    else SurfelColourManipulator<TSurfel::hasColourInformation>::write(surfel, colourMap[locId].toVector3());
#endif

#if DEBUG_CORRESPONDENCES
    // Store the position of the corresponding surfel (if any).
    int correspondingSurfelIndex = correspondenceMap[locId] - 1;
    surfel.correspondingSurfelPosition = correspondingSurfelIndex >= 0 ? surfels[correspondingSurfelIndex].position : surfel.position;
#endif

    newSurfels[newPointsPrefixSum[locId]] = surfel;
  }
}

/**
 * \brief TODO
 */
_CPU_AND_GPU_CODE_
inline void calculate_vertex_position(int locId, int width, const ITMIntrinsics& intrinsics, const float *depthMap, Vector3f *vertexMap)
{
  /*
  v(~u~) = D(~u~) K^{-1} (~u~^T,1)^T
         = D(~u~) (fx 0 px)^{-1} (ux) = D(~u~) ((ux - px) / fx)
                  (0 fy py)      (uy)          ((uy - py) / fy)
                  (0  0  1)      ( 1)          (             1)
  */
  int ux = locId % width, uy = locId / width;
  vertexMap[locId] = depthMap[locId] * Vector3f(
    (ux - intrinsics.projectionParamsSimple.px) / intrinsics.projectionParamsSimple.fx,
    (uy - intrinsics.projectionParamsSimple.py) / intrinsics.projectionParamsSimple.fy,
    1
  );
}

/**
 * \brief TODO
 */
_CPU_AND_GPU_CODE_
inline void clear_removal_mask(int surfelId, unsigned int *surfelRemovalMask)
{
  surfelRemovalMask[surfelId] = 0;
}

/**
 * \brief TODO
 */
template <typename TSurfel>
_CPU_AND_GPU_CODE_
inline void find_corresponding_surfel(int locId, const Matrix4f& invT, const float *depthMap, int depthMapWidth, const unsigned int *indexMap, const TSurfel *surfels,
                                      unsigned int *correspondenceMap, unsigned short *newPointsMask)
{
  // If the depth pixel is invalid, early out.
  float depth = depthMap[locId];
  if(fabs(depth + 1) <= 0.0001f)
  {
    correspondenceMap[locId] = 0;
    newPointsMask[locId] = 0;
    return;
  }

  // Otherwise, find corresponding surfels in the scene and pick the best one (if any).
  int bestSurfelIndex = -1;
  float bestSurfelConfidence = 0.0f;
  int ux = locId % depthMapWidth, uy = locId / depthMapWidth;
  for(int dy = 0; dy < 4; ++dy)
  {
    for(int dx = 0; dx < 4; ++dx)
    {
      int x = ux * 4 + dx;
      int y = uy * 4 + dy;
      int surfelIndex = indexMap[y * depthMapWidth * 4 + x] - 1;
      if(surfelIndex >= 0)
      {
        // TODO: Make this slightly more sophisticated, as per the paper.
        TSurfel surfel = surfels[surfelIndex];
        float surfelDepth = calculate_depth_from_pose(invT, surfel.position);

        const float deltaDepth = 0.01f;
        if(surfel.confidence > bestSurfelConfidence && fabs(surfelDepth - depth) <= deltaDepth)
        {
          bestSurfelIndex = surfelIndex;
          bestSurfelConfidence = surfel.confidence;
        }
      }
    }
  }

  // Record any corresponding surfel found, together with a flag indicating whether or not we need to add a new surfel.
  correspondenceMap[locId] = bestSurfelIndex >= 0 ? bestSurfelIndex + 1 : 0;
  newPointsMask[locId] = bestSurfelIndex >= 0 ? 0 : 1;

#if DEBUG_CORRESPONDENCES
  newPointsMask[locId] = 1;
#endif
}

/**
 * \brief TODO
 */
template <typename TSurfel>
_CPU_AND_GPU_CODE_
inline void fuse_matched_point(int locId, const unsigned int *correspondenceMap, const Matrix4f& T, const Vector3f *vertexMap,
                               const Vector4f *normalMap, const float *radiusMap, const Vector4u *colourMap, int timestamp,
                               TSurfel *surfels, int colourMapWidth)
{
  // TEMPORARY
  const float alpha = 1.0f;

  int surfelIndex = correspondenceMap[locId] - 1;
  if(surfelIndex >= 0)
  {
    TSurfel surfel = surfels[surfelIndex];

    const float newConfidence = surfel.confidence + alpha;
    surfel.position = (surfel.confidence * surfel.position + alpha * transform_point(T, vertexMap[locId])) / newConfidence;

    // TODO: Normal, radius, etc.

    Vector3u oldColour = SurfelColourManipulator<TSurfel::hasColourInformation>::read(surfel);

    // TEMPORARY: Read from the proper position in the colour map.
    Vector3u newColour;
    if(colourMapWidth == 320)
    {
        int x = (locId % 640) / 2;
        int y = (locId / 640) / 2;
        newColour = colourMap[y * 320 + x].toVector3();
    }
    else newColour = colourMap[locId].toVector3();

    Vector3u colour = ((surfel.confidence * oldColour.toFloat() + alpha * newColour.toFloat()) / newConfidence).toUChar();
    SurfelColourManipulator<TSurfel::hasColourInformation>::write(surfel, oldColour);

    surfel.confidence = newConfidence;
    surfel.timestamp = timestamp;

    surfels[surfelIndex] = surfel;
  }
}

/**
 * \brief TODO
 */
template <typename TSurfel>
_CPU_AND_GPU_CODE_
inline void mark_for_removal_if_unstable(int surfelId, const TSurfel *surfels, int timestamp, unsigned int *surfelRemovalMask)
{
  // TEMPORARY
  const float stableConfidence = 10.0f;
  TSurfel surfel = surfels[surfelId];
  if(surfel.confidence < stableConfidence && timestamp - surfel.timestamp > 5)
  {
    surfelRemovalMask[surfelId] = 1;
  }
}

}
