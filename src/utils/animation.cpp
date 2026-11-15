// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "animation.hpp"

#include "base/utils.hpp"
#include "base/log.hpp"

#include <stdio.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>

#include "loaders/gltfdata.hpp"
#include "animationextensions/footballanimationextension.hpp"

std::string emptyString = "";

namespace blunted {

  // todo: make default orientation changeable
  radian FixAngle(radian angle, bool modulateIntoRange = true) {
    // convert engine angle into football angle (different base orientation: 'down' on y instead of 'right' on x)
    radian newAngle = angle;
    newAngle += 0.5 * pi;
    if (modulateIntoRange) newAngle = ModulateIntoRange(-pi, pi, newAngle);
    return newAngle;
  }

  Animation::Animation() {
    frameCount = 0;
    // all humanoid movies are supposed to have a moving right foot at first (unless mirrored)
    currentFoot = e_Foot_Right;

    DirtyCache();
  }

  Animation::Animation(const Animation &src) {
    int animSize = src.nodeAnimations.size();
    for (int i = 0; i < animSize; i++) {
      nodeAnimations.push_back(new NodeAnimation(*src.nodeAnimations.at(i)));
    }

    frameCount = src.frameCount;
    name = src.name;

    // attention! shallow copy!
    extensions = src.extensions;

    std::shared_ptr<XMLTree> tmpCustomData(new XMLTree(*src.customData));
    customData = tmpCustomData;

    std::map<const char*, std::string>::const_iterator varCacheIter = src.variableCache.begin();
    while (varCacheIter != src.variableCache.end()) {
      char *varName = new char[256];
      memcpy(varName, varCacheIter->first, 256 * sizeof(char));
      variableCache.insert(std::pair<const char*, std::string>(varName, varCacheIter->second));
      varCacheIter++;
    }

    currentFoot = src.currentFoot;

    cache_translation_dirty = src.cache_translation_dirty;
    cache_translation = src.cache_translation;
    cache_incomingMovement_dirty = src.cache_incomingMovement_dirty;
    cache_incomingMovement = src.cache_incomingMovement;
    cache_incomingVelocity_dirty = src.cache_incomingVelocity_dirty;
    cache_incomingVelocity = src.cache_incomingVelocity;
    cache_outgoingDirection_dirty = src.cache_outgoingDirection_dirty;
    cache_outgoingDirection = src.cache_outgoingDirection;
    cache_outgoingMovement_dirty = src.cache_outgoingMovement_dirty;
    cache_outgoingMovement = src.cache_outgoingMovement;
    cache_rangedOutgoingMovement_dirty = src.cache_rangedOutgoingMovement_dirty;
    cache_rangedOutgoingMovement = src.cache_rangedOutgoingMovement;
    cache_outgoingVelocity_dirty = src.cache_outgoingVelocity_dirty;
    cache_outgoingVelocity = src.cache_outgoingVelocity;
    cache_angle_dirty = src.cache_angle_dirty;
    cache_angle = src.cache_angle;
    cache_incomingBodyAngle_dirty = src.cache_incomingBodyAngle_dirty;
    cache_incomingBodyAngle = src.cache_incomingBodyAngle;
    cache_outgoingBodyAngle_dirty = src.cache_outgoingBodyAngle_dirty;
    cache_outgoingBodyAngle = src.cache_outgoingBodyAngle;
    cache_incomingBodyDirection_dirty = src.cache_incomingBodyDirection_dirty;
    cache_incomingBodyDirection = src.cache_incomingBodyDirection;
    cache_outgoingBodyDirection_dirty = src.cache_outgoingBodyDirection_dirty;
    cache_outgoingBodyDirection = src.cache_outgoingBodyDirection;

    cache_AnimType = src.cache_AnimType;
  }

  Animation::~Animation() {
    Reset();
  }


  void Animation::DirtyCache() {
    cache_translation_dirty = true;
    cache_incomingMovement_dirty = true;
    cache_incomingVelocity_dirty = true;
    cache_outgoingDirection_dirty = true;
    cache_outgoingMovement_dirty = true;
    cache_rangedOutgoingMovement_dirty = true;
    cache_outgoingVelocity_dirty = true;
    cache_angle_dirty = true;
    cache_incomingBodyAngle_dirty = true;
    cache_outgoingBodyAngle_dirty = true;
    cache_incomingBodyDirection_dirty = true;
    cache_outgoingBodyDirection_dirty = true;
  }

  int Animation::GetFrameCount() const {
    return frameCount;
  }

  bool Animation::GetKeyFrame(const std::string &nodeName, int frame, Quaternion &orientation, Vector3 &position, bool getOrientation, bool getPosition) const {
    int animSize = nodeAnimations.size();
    for (int i = 0; i < animSize; i++) {
      if (nodeAnimations.at(i)->nodeName == nodeName) {
        GetInterpolatedValues(nodeAnimations.at(i)->animation, frame, orientation, position, getOrientation, getPosition);
        return nodeAnimations.at(i)->animation.find(frame) != nodeAnimations.at(i)->animation.end();
      }
    }
    return false;
  }

  void Animation::SetKeyFrame(const std::string &nodeName, int frame, const Quaternion &orientation, const Vector3 &position) {
    if (frame >= frameCount) frameCount = frame + 1;

    NodeAnimation *nodeAnimation = 0;

    // find node
    int animSize = nodeAnimations.size();
    for (int i = 0; i < animSize; i++) {
      if (nodeAnimations.at(i)->nodeName == nodeName) {
        nodeAnimation = nodeAnimations.at(i);
        break;
      }
    }

    // node doesn't exist yet?
    if (nodeAnimation == 0) {
      nodeAnimation = new NodeAnimation();
      nodeAnimation->nodeName = nodeName;
      nodeAnimations.push_back(nodeAnimation);
    }

    // find frame
    const std::map<int, KeyFrame>::iterator animIter = nodeAnimation->animation.find(frame);
    if (animIter != nodeAnimation->animation.end()) {
      // change
      animIter->second.orientation = orientation;
      animIter->second.position = position;
    } else {
      // insert
      KeyFrame keyFrame;
      keyFrame.orientation = orientation;
      keyFrame.position = position;
      nodeAnimation->animation.insert(std::pair<int, KeyFrame>(frame, keyFrame));
    }

    DirtyCache();
  }

  void Animation::DeleteKeyFrame(const std::string &nodeName, int frame) {
    // iterate nodes
    int animSize = nodeAnimations.size();
    for (int i = 0; i < animSize; i++) {
      NodeAnimation *nodeAnimation = nodeAnimations.at(i);
      if (nodeAnimation->nodeName == nodeName) {
        // find frame
        const std::map<int, KeyFrame>::iterator animIter = nodeAnimation->animation.find(frame);
        if (animIter != nodeAnimation->animation.end()) {
          nodeAnimation->animation.erase(animIter);
          break;
        }
        if (animIter == nodeAnimation->animation.end()) printf("ERROR! keyframe does not exist\n");
      }
    }

    DirtyCache();
  }

  void Animation::GetInterpolatedValues(const std::map<int, KeyFrame> &animation, int frame, Quaternion &orientation, Vector3 &position, bool getOrientation, bool getPosition) const {
    position = Vector3(0);
    orientation = QUATERNION_IDENTITY;
    if (animation.empty()) return;

    const int totalFrames = GetFrameCount();
    if (frame > 0 && frame < totalFrames) {
      std::map<int, KeyFrame>::const_iterator upper = animation.lower_bound(frame);
      if (upper == animation.begin()) {
        if (getOrientation) orientation = upper->second.orientation;
        if (getPosition) position = upper->second.position;
      } else if (upper == animation.end()) {
        std::map<int, KeyFrame>::const_iterator prev = std::prev(upper);
        if (getOrientation) orientation = prev->second.orientation;
        if (getPosition) position = prev->second.position;
      } else {
        std::map<int, KeyFrame>::const_iterator prev = std::prev(upper);
        int distance = upper->first - prev->first;
        int distance1 = frame - prev->first;
        int distance2 = upper->first - frame;
        float ratio1 = 1 - (distance1 * 1.0) / (distance * 1.0);
        float ratio2 = 1 - (distance2 * 1.0) / (distance * 1.0);
        float bias = (distance1 * 1.0) / (distance * 1.0);
        if (getOrientation) {
          orientation = prev->second.orientation.GetSlerped(bias, upper->second.orientation);
        }
        if (getPosition) {
          position = prev->second.position * ratio1 + upper->second.position * ratio2;
        }
      }
    } else if (frame >= totalFrames) {
      std::map<int, KeyFrame>::const_iterator last = std::prev(animation.end());
      if (last == animation.begin()) {
        if (getOrientation) orientation = last->second.orientation;
        if (getPosition) position = last->second.position;
        return;
      }
      std::map<int, KeyFrame>::const_iterator secondLast = std::prev(last);
      float dist1 = last->first - secondLast->first;
      float dist2 = frame - last->first;
      float bias = 1 + ((1 / dist1) * dist2);
      if (getOrientation) orientation = secondLast->second.orientation.GetSlerped(bias, last->second.orientation);
      if (getPosition) position = secondLast->second.position * (1 - bias) + last->second.position * bias;
    } else if (frame <= 0) {
      std::map<int, KeyFrame>::const_iterator animIter = animation.begin();
      if (getOrientation) orientation = animIter->second.orientation;
      if (getPosition) position = animIter->second.position;
    }
  }

  void Animation::ConvertToStartFacingForwardIfIdle() {

    radian incomingBodyAngle = GetIncomingBodyAngle();

    // only applies to anims starting out idle
    if (GetIncomingVelocity() >= 1.8) return;

    // rotate player pos
    std::map<int, KeyFrame> &player = nodeAnimations.at(0)->animation;
    std::map<int, KeyFrame>::iterator animIter = player.begin();
    while (animIter != player.end()) {
      //animIter->second.position.Print();
      animIter->second.position.Rotate2D(-incomingBodyAngle);
      animIter++;
    }

    // rotate body dir
    std::map<int, KeyFrame> &body = nodeAnimations.at(1)->animation;
    animIter = body.begin();
    while (animIter != body.end()) {
      //animIter->second.position.Print();
      Quaternion rotation(QUATERNION_IDENTITY);
      Quaternion zRot;
      zRot.SetAngleAxis(-incomingBodyAngle, Vector3(0, 0, 1));
      rotation = zRot * rotation;
      animIter->second.orientation = rotation * animIter->second.orientation;
      //animIter->second.angles.coords[2] -= GetIncomingBodyAngle();
      animIter++;
    }

    std::map < std::string, std::shared_ptr<AnimationExtension> >::iterator extensionIter = extensions.begin();
    while (extensionIter != extensions.end()) {
      extensionIter->second->Rotate2D(-incomingBodyAngle);
      extensionIter++;
    }

    // variables
    Vector3 newBallDirVec = GetVariable("balldirection") != "" ? GetVectorFromString(GetVariable("balldirection")).GetRotated2D(-incomingBodyAngle) : 0;
    Vector3 newIncomingBallDirVec = GetVariable("incomingballdirection") != "" ? GetVectorFromString(GetVariable("incomingballdirection")).GetRotated2D(-incomingBodyAngle) : 0;
    Vector3 newBumpDirVec = GetVariable("bumpdirection") != "" ? GetVectorFromString(GetVariable("bumpdirection")).GetRotated2D(-incomingBodyAngle) : 0;
    SetVariable("balldirection", GetStringFromVector(newBallDirVec));
    SetVariable("incomingballdirection", GetStringFromVector(newIncomingBallDirVec));
    SetVariable("bumpdirection", GetStringFromVector(newBumpDirVec));

    DirtyCache();
  }

  void Animation::Invert() {

    NodeAnimation *nodeAnimation = 0;

    // simple keyframe-to-keyframe version
    int animSize = nodeAnimations.size();
    for (int i = 0; i < animSize; i++) {
      nodeAnimation = nodeAnimations.at(i);

      std::map<int, KeyFrame>::iterator keyIter = nodeAnimation->animation.begin();
      while (keyIter != nodeAnimation->animation.end()) {
        Quaternion &orient = (*keyIter).second.orientation;

        radian x, y, z;
        orient.GetAngles(x, y, z);
        Quaternion rotX, rotY, rotZ;
        rotX.SetAngleAxis(-x, Vector3(1, 0, 0));
        rotY.SetAngleAxis(-y, Vector3(0, 1, 0));
        rotZ.SetAngleAxis(-z, Vector3(0, 0, 1));
        orient = rotX * rotY * rotZ;

        keyIter++;
      }
    }
  }

  void Animation::Apply(const std::map < const std::string, blunted::intrusive_ptr<Node> > &nodeMap, int frame, int timeOffset_ms, bool smooth, float smoothFactor, /*const std::shared_ptr<Animation> previousAnimation, int smoothFrames, */const Vector3 &basePos, radian baseRot, std::map < std::string, BiasedOffset > &offsets, MovementHistory *movementHistory, int timeDiff_ms, bool noPos, bool updateSpatial) {

    // simple keyframe-to-keyframe version

    NodeAnimation *nodeAnimation = 0;

    float bias = 0.5f;
    if (timeOffset_ms != -1) bias = clamp(timeOffset_ms / 10.0f, 0.0f, 1.0f);

    const bool hasOffsets = !offsets.empty();
    unsigned int animSize = nodeAnimations.size();
    for (unsigned int i = 0; i < animSize; i++) {

      nodeAnimation = nodeAnimations.at(i);
      const std::string &boneName = nodeAnimation->nodeName;
      const bool isPlayerBone = (boneName.compare("player") == 0);

      Quaternion orientation;
      Vector3 position;

      if (bias <= 0.0f) {
        GetInterpolatedValues(nodeAnimation->animation, frame, orientation, position);
        orientation.Normalize();
      } else if (bias >= 1.0f) {
        GetInterpolatedValues(nodeAnimation->animation, frame + 1, orientation, position);
        orientation.Normalize();
      } else {
        Quaternion orientation_pre, orientation_post;
        Vector3 position_pre, position_post;
        GetInterpolatedValues(nodeAnimation->animation, frame,     orientation_pre,  position_pre);
        GetInterpolatedValues(nodeAnimation->animation, frame + 1, orientation_post, position_post);
        orientation_pre.MakeSameNeighborhood(orientation_post);
        orientation = orientation_pre.GetLerped(bias, orientation_post).GetNormalized();
        position = position_pre * (1.0f - bias) + position_post * bias;
      }

      std::map < const std::string, blunted::intrusive_ptr<Node> >::const_iterator nodeIt = nodeMap.find(boneName);
      assert(nodeIt != nodeMap.end());
      Node *targetNode = nodeIt->second.get();

      if (isPlayerBone) {
        if (noPos) {
          position.coords[0] = 0;
          position.coords[1] = 0;
        } else {
          position.Rotate2D(baseRot);
        }
      } else if (boneName.compare("body") == 0) {
        Quaternion rotZ;
        rotZ.SetAngleAxis(baseRot, Vector3(0, 0, 1));
        orientation = rotZ * orientation;
        orientation.Normalize();
      }

      // offset
      if (hasOffsets) {
        std::map < std::string, BiasedOffset >::iterator iter = offsets.find(boneName);
        if (iter != offsets.end()) {
          iter->second.orientation.MakeSameNeighborhood(orientation);
          if (iter->second.isRelative) {
            orientation = orientation.GetLerped(iter->second.bias, iter->second.orientation * orientation).GetNormalized();
          } else {
            orientation = orientation.GetLerped(iter->second.bias, iter->second.orientation).GetNormalized();
          }
        }
      }


      // SMOOTHING

      if (smooth) {

        assert(movementHistory);
        MovementHistoryEntry *movementHistoryEntry = 0;

        if (i < movementHistory->size() && (*movementHistory)[i].nodeName.compare(boneName) == 0) {
          movementHistoryEntry = &(*movementHistory)[i];
        } else {
          for (unsigned int node = 0; node < movementHistory->size(); node++) {
            if (movementHistory->at(node).nodeName.compare(boneName) == 0) {
              movementHistoryEntry = &movementHistory->at(node);
              break;
            }
          }
        }
        if (movementHistoryEntry == 0) { // not in movementhistory yet; add
          MovementHistoryEntry newEntry;
          newEntry.nodeName = boneName;
          newEntry.position = position;
          newEntry.orientation = orientation;
          newEntry.timeDiff_ms = 10;
          movementHistory->push_back(newEntry);
          movementHistoryEntry = &movementHistory->back();
        }

        float beginBias = std::sqrt(curve(1.0f - NormalizedClamp(frame, 0, 8), 1.0f));
        float currentBias = 0.0f + beginBias * smoothFactor * 0.5f;

        if (!isPlayerBone) {

          const Quaternion &previousOrientation = movementHistoryEntry->orientation;
          Quaternion currentOrientation = targetNode->GetRotation();
          currentOrientation.MakeSameNeighborhood(previousOrientation);

          if (timeDiff_ms > 0) {

            orientation = orientation.GetSlerped(currentBias, currentOrientation).GetNormalized();

            // maximum rotational velocity
            float dot = orientation.MakeSameNeighborhood(currentOrientation);
            radian angle_per_second = 2.0f * acos(clamp(dot, -1.0f, 1.0f)) / ((float)timeDiff_ms * 0.001f);
            radian maxAngle_per_second = 7.5f * pi;
            if (boneName.compare("left_elbow") == 0) maxAngle_per_second *= 1.2f;
            else if (boneName.compare("right_elbow") == 0) maxAngle_per_second *= 1.2f;
            else if (boneName.compare("left_knee") == 0) maxAngle_per_second *= 1.2f;
            else if (boneName.compare("right_knee") == 0) maxAngle_per_second *= 1.2f;
            else if (boneName.compare("left_ankle") == 0) maxAngle_per_second *= 1.6f;
            else if (boneName.compare("right_ankle") == 0) maxAngle_per_second *= 1.6f;
            maxAngle_per_second = (0.3f + 0.7f * (1.0f - beginBias)) * maxAngle_per_second;
            if (angle_per_second > maxAngle_per_second) {
              float allowFraction = maxAngle_per_second / angle_per_second;
              Quaternion desiredRotation = currentOrientation.GetRotationTo(orientation).GetNormalized();
              orientation = (desiredRotation.GetRotationMultipliedBy(allowFraction) * currentOrientation).GetNormalized();
            }

          } // timeDiff_ms > 0

          movementHistoryEntry->orientation = currentOrientation;
          movementHistoryEntry->timeDiff_ms = timeDiff_ms;

        } else {

          const Vector3 &previousPosition = movementHistoryEntry->position;
          Vector3 currentPosition = targetNode->GetPosition();

          if (timeDiff_ms > 0 && beginBias > 0.01f) {

            // smooth
            position.coords[2] = position.coords[2] * (1.0f - currentBias) + currentPosition.coords[2] * currentBias;

            float maxMetersPerSec = 2.8f;
            if (GetVariable("outgoing_special_state").compare("") != 0) maxMetersPerSec = 6.0f;
            float allowedDistance = maxMetersPerSec * ((float)timeDiff_ms * 0.001f);

            float newZ = position.coords[2] + basePos.coords[2];
            float desiredDistance = fabs(newZ - currentPosition.coords[2]);

            float zBias = 1.0f;
            if (desiredDistance > allowedDistance) zBias = allowedDistance / desiredDistance;

            newZ = (newZ * zBias + currentPosition.coords[2] * (1.0f - zBias)) * beginBias +
                   newZ * (1.0f - beginBias);

            // only height
            position = position.Get2D() + Vector3(0, 0, newZ);

          }

          movementHistoryEntry->position = currentPosition;
          movementHistoryEntry->timeDiff_ms = timeDiff_ms;

        }

      } // smoothing


      if (!isPlayerBone) {
        Quaternion currentOrientation = targetNode->GetRotation();
        orientation.MakeSameNeighborhood(currentOrientation);
        targetNode->SetRotation(orientation, false);
      } else {
        targetNode->SetPosition(position + basePos, false);
      }

    }

    if (updateSpatial) (*nodeMap.find(nodeAnimations.at(0)->nodeName)).second->RecursiveUpdateSpatialData(e_SpatialDataType_Both);
  }

  void Animation::Shift(int fromFrame, int offset) { // todo: offset does not yet work
    if (offset == 1) {
      bool somethingShifted = false;
      int animSize = nodeAnimations.size();
      for (int i = 0; i < animSize; i++) {
        NodeAnimation *nodeAnimation = nodeAnimations.at(i);
        std::map<int, KeyFrame>::iterator animIter = nodeAnimation->animation.begin();
        std::map<int, KeyFrame> newAnimation;
        while (animIter != nodeAnimation->animation.end()) {

          //printf("%i, ", animIter->first);
          KeyFrame keyFrame = animIter->second;
          int frameNum = animIter->first;
          if (animIter->first >= fromFrame) {
            frameNum++; // shift
            somethingShifted = true;
          }
          newAnimation.insert(std::pair<int, KeyFrame>(frameNum, keyFrame));
          animIter++;
        }
        nodeAnimation->animation = newAnimation;
      }

      if (somethingShifted) {
        frameCount++;
        DirtyCache();
      }
    }

    if (offset == -1) {
      bool somethingShifted = false;
      int animSize = nodeAnimations.size();
      for (int i = 0; i < animSize; i++) {
        NodeAnimation *nodeAnimation = nodeAnimations.at(i);
        std::map<int, KeyFrame>::iterator animIter = nodeAnimation->animation.begin();
        std::map<int, KeyFrame> newAnimation;
        while (animIter != nodeAnimation->animation.end()) {

          //printf("%i, ", animIter->first);
          KeyFrame keyFrame = animIter->second;
          int frameNum = animIter->first;
          if (animIter->first != fromFrame) {
            if (animIter->first > fromFrame) {
              frameNum--; // shift
              somethingShifted = true;
            }
            newAnimation.insert(std::pair<int, KeyFrame>(frameNum, keyFrame));
          }
          animIter++;
        }
        nodeAnimation->animation = newAnimation;
      }

      if (somethingShifted) {
        frameCount--;
        DirtyCache();
      }
    }

    std::map < std::string, std::shared_ptr<AnimationExtension> >::iterator extensionIter = extensions.begin();
    while (extensionIter != extensions.end()) {
      extensionIter->second->Shift(fromFrame, offset);
      extensionIter++;
    }
  }

  Vector3 Animation::GetTranslation() const {
    if (cache_translation_dirty) {
      cache_translation = ((--nodeAnimations.at(0)->animation.end())->second.position -
                           nodeAnimations.at(0)->animation.begin()->second.position);
      cache_translation.coords[2] = 0;
      cache_translation_dirty = false;
    }
    return cache_translation;
  }

  Vector3 Animation::GetIncomingMovement() const {
    // todo: check if correct with idle incoming velo + weird incoming body rot
    if (cache_incomingMovement_dirty) {
      if (nodeAnimations.at(0)->animation.size() > 1) {
        cache_incomingMovement = ((++nodeAnimations.at(0)->animation.begin())->second.position -
                                  nodeAnimations.at(0)->animation.begin()->second.position) /
                                 ((++nodeAnimations.at(0)->animation.begin())->first -
                                  nodeAnimations.at(0)->animation.begin()->first * 1.0) * 100;
        cache_incomingMovement.coords[2] = 0;
      } else {
        cache_incomingMovement = Vector3(0);
      }
      cache_incomingMovement_dirty = false;
    }
    return cache_incomingMovement;
  }

  float Animation::GetIncomingVelocity() const {
    if (cache_incomingVelocity_dirty) {
      if (nodeAnimations.at(0)->animation.size() > 1) {
        Vector3 result = ((++nodeAnimations.at(0)->animation.begin())->second.position -
                          nodeAnimations.at(0)->animation.begin()->second.position) /
                         ((++nodeAnimations.at(0)->animation.begin())->first -
                          nodeAnimations.at(0)->animation.begin()->first * 1.0) * 100;
        result.coords[2] = 0;
        cache_incomingVelocity = result.GetLength();
        if (cache_incomingVelocity < 1.8) cache_incomingVelocity = 0;
        else if (cache_incomingVelocity >= 1.8 && cache_incomingVelocity < 4.2) cache_incomingVelocity = 3.5;
        else if (cache_incomingVelocity >= 4.2 && cache_incomingVelocity < 6.0) cache_incomingVelocity = 5.0;
        else if (cache_incomingVelocity >= 6.0) cache_incomingVelocity = 7.0;
      } else {
        cache_incomingVelocity = 0;
      }
      cache_incomingVelocity_dirty = false;
    }
    return cache_incomingVelocity;
  }

  Vector3 Animation::GetOutgoingMovement() const {
    if (cache_outgoingMovement_dirty) {

      if (nodeAnimations.at(0)->animation.size() > 1) {
        cache_outgoingMovement = ((--nodeAnimations.at(0)->animation.end())->second.position -
                                  (--(--nodeAnimations.at(0)->animation.end()))->second.position) /
                                 ((--nodeAnimations.at(0)->animation.end())->first -
                                  (--(--nodeAnimations.at(0)->animation.end()))->first * 1.0) * 100;
        cache_outgoingMovement.coords[2] = 0;
      } else {
        cache_outgoingMovement = Vector3(0);
      }

      cache_outgoingMovement_dirty = false;
    }
    return cache_outgoingMovement;
  }

  Vector3 Animation::GetRangedOutgoingMovement() const {
    if (cache_rangedOutgoingMovement_dirty || cache_outgoingVelocity_dirty || cache_angle_dirty) {

      if (nodeAnimations.at(0)->animation.size() > 1) {
        cache_rangedOutgoingMovement = Vector3(0, -GetOutgoingVelocity(), 0).GetRotated2D(GetOutgoingAngle());
      } else {
        cache_rangedOutgoingMovement = Vector3(0);
      }

      cache_rangedOutgoingMovement_dirty = false;
    }
    return cache_rangedOutgoingMovement;
  }

  Vector3 Animation::GetOutgoingDirection() const {
    if (cache_outgoingDirection_dirty || cache_angle_dirty) {
      cache_outgoingDirection = Vector3(0, -1, 0).GetRotated2D(GetOutgoingAngle());
      cache_outgoingDirection_dirty = false;
    }
    return cache_outgoingDirection;
  }

  Vector3 Animation::GetIncomingBodyDirection() const {
    if (cache_incomingBodyDirection_dirty || cache_incomingBodyAngle_dirty) {
      cache_incomingBodyDirection = Vector3(0, -1, 0).GetRotated2D(GetIncomingBodyAngle());
      cache_incomingBodyDirection_dirty = false;
    }
    return cache_incomingBodyDirection;
  }

  Vector3 Animation::GetOutgoingBodyDirection() const {
    if (cache_outgoingBodyDirection_dirty || cache_outgoingBodyAngle_dirty) {
      cache_outgoingBodyDirection = Vector3(0, -1, 0).GetRotated2D(GetOutgoingBodyAngle());
      cache_outgoingBodyDirection_dirty = false;
    }
    return cache_outgoingBodyDirection;
  }

  float Animation::GetOutgoingVelocity() const {
    if (cache_outgoingVelocity_dirty) {
      if (nodeAnimations.at(0)->animation.size() > 1) {
        Vector3 result = ((--nodeAnimations.at(0)->animation.end())->second.position -
                          (--(--nodeAnimations.at(0)->animation.end()))->second.position) /
                         ((--nodeAnimations.at(0)->animation.end())->first -
                          (--(--nodeAnimations.at(0)->animation.end()))->first * 1.0) * 100;
        result.coords[2] = 0;
        cache_outgoingVelocity = result.GetLength();
        if (cache_outgoingVelocity < 1.8) cache_outgoingVelocity = 0.0;
        else if (cache_outgoingVelocity >= 1.8 && cache_outgoingVelocity < 4.2) cache_outgoingVelocity = 3.5;
        else if (cache_outgoingVelocity >= 4.2 && cache_outgoingVelocity < 6.0) cache_outgoingVelocity = 5.0;
        else if (cache_outgoingVelocity >= 6.0) cache_outgoingVelocity = 7.0;
      } else {
        cache_outgoingVelocity = 0;
      }
      cache_outgoingVelocity_dirty = false;
    }
    return cache_outgoingVelocity;
  }

  radian Animation::GetOutgoingAngle() const {
    if (cache_angle_dirty || cache_outgoingVelocity_dirty) {
      if (GetOutgoingVelocity() >= 1.8) {
        //if (nodeAnimations.at(0)->animation.size() > 1) {

        // full player rotation - last move
        Vector3 lastMoveVector = (--nodeAnimations.at(0)->animation.end())->second.position -
                                 (--(--nodeAnimations.at(0)->animation.end()))->second.position;
        cache_angle = FixAngle(lastMoveVector.GetAngle2D(), true);

        // if angle is close to 180 degrees, we can't be sure if we want 180 or -180 deg. use body angle as hint.
        if (cache_angle < -0.95f * pi || cache_angle > 0.95f * pi) {
          radian x, y, z;
          (--(nodeAnimations.at(1)->animation.end()))->second.orientation.GetAngles(x, y, z);
          if (signSide(cache_angle) != signSide(z)) {
            cache_angle = pi * 0.99f * signSide(z);
          } else {
            cache_angle = clamp(cache_angle, -0.99f * pi, 0.99f * pi); // also do this if the side is already correct: we want to be a little away from pi, else vectors based on this don't have a clear sidedness
          }
        }

      } else {

        if (nodeAnimations.at(1)->animation.size() > 0) {
          // body rotation
          radian x, y, z;
          (--(nodeAnimations.at(1)->animation.end()))->second.orientation.GetAngles(x, y, z);
          cache_angle = z;

          // lying on the ground? (buggy)
          // float threshold = pi * 0.35;
          // if (fabs(x) > threshold || fabs(y) > threshold) {
          //   Vector3 quatDirection; quatDirection = (--(nodeAnimations.at(1)->animation.end()))->second.orientation;
          //   //quatDirection = -quatDirection;
          //   cache_angle = FixAngle(quatDirection.Get2D().GetNormalized().GetAngle2D());
          // }

          cache_angle = ModulateIntoRange(-pi, pi, cache_angle);

        } else {
          cache_angle = 0;
        }

      }

      cache_angle_dirty = false;
    }
    return cache_angle;
  }

  radian Animation::GetIncomingBodyAngle() const {
    if (cache_incomingBodyAngle_dirty) {
      if (nodeAnimations.at(1)->animation.size() > 0) {

        // body rotation
        radian x, y, z;
        (nodeAnimations.at(1)->animation.begin())->second.orientation.GetAngles(x, y, z);
        cache_incomingBodyAngle = z;

        // lying on the ground? (buggy)
        // float threshold = pi * 0.35;
        // if (fabs(x) > threshold || fabs(y) > threshold) {
        //   Vector3 quatDirection; quatDirection = (nodeAnimations.at(1)->animation.begin())->second.orientation;
        //   //quatDirection = -quatDirection;
        //   cache_incomingBodyAngle = FixAngle(quatDirection.Get2D().GetNormalized().GetAngle2D());
        // }

        cache_incomingBodyAngle = ModulateIntoRange(-pi, pi, cache_incomingBodyAngle);
        //printf("angle: %f\n", cache_incomingBodyAngle);

      } else {
        cache_incomingBodyAngle = 0;
      }

      cache_incomingBodyAngle_dirty = false;
    }
    return cache_incomingBodyAngle;
  }

  radian Animation::GetOutgoingBodyAngle() const {
    if (cache_outgoingBodyAngle_dirty || cache_angle_dirty || cache_outgoingVelocity_dirty) {
      if (GetOutgoingVelocity() >= 1.8) {
        if (nodeAnimations.at(1)->animation.size() > 0) {

          // body rotation
          radian x, y, z;
          (--(nodeAnimations.at(1)->animation.end()))->second.orientation.GetAngles(x, y, z);
          cache_outgoingBodyAngle = z;

/* impossible while moving, right?
          // lying on the ground? (buggy)
          float threshold = pi * 0.35;
          if (fabs(x) > threshold || fabs(y) > threshold) {
            Vector3 quatDirection; quatDirection = (--(nodeAnimations.at(1)->animation.end()))->second.orientation;
            //quatDirection = -quatDirection;
            cache_outgoingBodyAngle = FixAngle(quatDirection.Get2D().GetNormalized().GetAngle2D());
          }
*/
          cache_outgoingBodyAngle -= GetOutgoingAngle();

          cache_outgoingBodyAngle = ModulateIntoRange(-pi, pi, cache_outgoingBodyAngle);

        } else {
          cache_outgoingBodyAngle = 0;
        }

      } else { // new
        cache_outgoingBodyAngle = 0; // new
      } // new

      cache_outgoingBodyAngle_dirty = false;
    }
    return cache_outgoingBodyAngle;
  }

  e_Foot Animation::GetOutgoingFoot() const {
    std::string foot = GetVariable("steps");
    e_Foot curFoot = GetCurrentFoot();
    int steps = 1;
    if (foot != "") {
      steps = atoi(foot.c_str());
    }
    if (is_odd(steps)) {
      if (curFoot == e_Foot_Left) {
        return e_Foot_Right;
      } else {
        return e_Foot_Left;
      }
    } else {
      if (curFoot == e_Foot_Right) {
        return e_Foot_Right;
      } else {
        return e_Foot_Left;
      }
    }

    // default value to please the compiler
    return e_Foot_Right;
  }


  void Animation::Reset() {
    int animSize = nodeAnimations.size();
    for (int i = 0; i < animSize; i++) {
      delete nodeAnimations.at(i);
    }

    customData.reset();

    std::map<const char*, std::string>::const_iterator iter = variableCache.begin();
    while (iter != variableCache.end()) {
      delete [] (*iter).first;
      iter++;
    }
    variableCache.clear();
    nodeAnimations.clear();
    extensions.clear();
    frameCount = 0;

    DirtyCache();
  }

  void Animation::LoadData(std::vector < std::vector<std::string> > &file) {

    for (unsigned int line = 0; line < file.size(); line++) {

      unsigned int key = 1;
      while (key < file.at(line).size()) {
        int frame = int(round(atoi(file.at(line).at(key).c_str()) * 1.0));
        Quaternion orientation(QUATERNION_IDENTITY);

        if (line != 0) { // limbs, only rotations
          orientation.elements[0] = atof(file.at(line).at(key + 1).c_str());
          orientation.elements[1] = atof(file.at(line).at(key + 2).c_str());
          orientation.elements[2] = atof(file.at(line).at(key + 3).c_str());
          orientation.elements[3] = atof(file.at(line).at(key + 4).c_str());

          key += 5;
        }

        Vector3 position(0);

        if (line == 0) { // player, only position
          position.coords[0] = atof(file.at(line).at(key + 1).c_str());
          position.coords[1] = atof(file.at(line).at(key + 2).c_str());
          position.coords[2] = atof(file.at(line).at(key + 3).c_str());

          key += 4;
        }

        SetKeyFrame(file.at(line).at(0), frame, orientation, position);
      }
    }
  }

  namespace {

    struct DecodedSamplerTrack {
      std::vector<float> times;
      std::vector<float> values;
      int numComp = 0;
      std::string interpolation = "LINEAR";
    };

    Vector3 SampleGLTFVec3(const DecodedSamplerTrack &tr, float t, const Vector3 &fallback) {
      size_t n = tr.times.size();
      if (n == 0 || tr.numComp < 3) return fallback;
      bool cubic = (tr.interpolation == "CUBICSPLINE") && (tr.values.size() >= n * 9);
      if (!cubic && tr.values.size() < n * 3) return fallback;

      auto getVal = [&](size_t idx, int slot) -> Vector3 {
        size_t base = cubic ? (idx * 9 + slot * 3) : (idx * 3);
        return Vector3(tr.values[base + 0], tr.values[base + 1], tr.values[base + 2]);
      };

      if (n == 1 || t <= tr.times.front()) return getVal(0, 1);
      if (t >= tr.times.back()) return getVal(n - 1, 1);

      auto it = std::upper_bound(tr.times.begin(), tr.times.end(), t);
      size_t i1 = static_cast<size_t>(std::distance(tr.times.begin(), it));
      size_t i0 = (i1 > 0) ? (i1 - 1) : 0;
      float t0 = tr.times[i0];
      float t1 = tr.times[i1];
      float dt = t1 - t0;
      if (dt <= 1e-6f || tr.interpolation == "STEP") return getVal(i0, 1);

      float s = clamp((t - t0) / dt, 0.0f, 1.0f);
      if (cubic) {
        float s2 = s * s;
        float s3 = s2 * s;
        Vector3 v0 = getVal(i0, 1);
        Vector3 b0 = getVal(i0, 2) * dt;
        Vector3 a1 = getVal(i1, 0) * dt;
        Vector3 v1 = getVal(i1, 1);
        return v0 * (2.0f * s3 - 3.0f * s2 + 1.0f) +
               b0 * (s3 - 2.0f * s2 + s) +
               v1 * (-2.0f * s3 + 3.0f * s2) +
               a1 * (s3 - s2);
      }

      Vector3 v0 = getVal(i0, 1);
      Vector3 v1 = getVal(i1, 1);
      return v0 * (1.0f - s) + v1 * s;
    }

    Quaternion SampleGLTFQuat(const DecodedSamplerTrack &tr, float t, const Quaternion &fallback) {
      size_t n = tr.times.size();
      if (n == 0 || tr.numComp < 4) return fallback;
      bool cubic = (tr.interpolation == "CUBICSPLINE") && (tr.values.size() >= n * 12);
      if (!cubic && tr.values.size() < n * 4) return fallback;

      auto getRaw = [&](size_t idx, int slot) -> Quaternion {
        size_t base = cubic ? (idx * 12 + slot * 4) : (idx * 4);
        return Quaternion(tr.values[base + 0], tr.values[base + 1], tr.values[base + 2], tr.values[base + 3]);
      };

      if (n == 1 || t <= tr.times.front()) return getRaw(0, 1).GetNormalized();
      if (t >= tr.times.back()) return getRaw(n - 1, 1).GetNormalized();

      auto it = std::upper_bound(tr.times.begin(), tr.times.end(), t);
      size_t i1 = static_cast<size_t>(std::distance(tr.times.begin(), it));
      size_t i0 = (i1 > 0) ? (i1 - 1) : 0;
      float t0 = tr.times[i0];
      float t1 = tr.times[i1];
      float dt = t1 - t0;
      if (dt <= 1e-6f || tr.interpolation == "STEP") return getRaw(i0, 1).GetNormalized();

      float s = clamp((t - t0) / dt, 0.0f, 1.0f);
      if (cubic) {
        float s2 = s * s;
        float s3 = s2 * s;
        Quaternion v0 = getRaw(i0, 1);
        Quaternion b0 = getRaw(i0, 2);
        Quaternion a1 = getRaw(i1, 0);
        Quaternion v1 = getRaw(i1, 1);
        float c0 = 2.0f * s3 - 3.0f * s2 + 1.0f;
        float c1 = (s3 - 2.0f * s2 + s) * dt;
        float c2 = -2.0f * s3 + 3.0f * s2;
        float c3 = (s3 - s2) * dt;
        Quaternion q(
          c0 * v0.elements[0] + c1 * b0.elements[0] + c2 * v1.elements[0] + c3 * a1.elements[0],
          c0 * v0.elements[1] + c1 * b0.elements[1] + c2 * v1.elements[1] + c3 * a1.elements[1],
          c0 * v0.elements[2] + c1 * b0.elements[2] + c2 * v1.elements[2] + c3 * a1.elements[2],
          c0 * v0.elements[3] + c1 * b0.elements[3] + c2 * v1.elements[3] + c3 * a1.elements[3]
        );
        return q.GetNormalized();
      }

      Quaternion q0 = getRaw(i0, 1).GetNormalized();
      Quaternion q1 = getRaw(i1, 1).GetNormalized();
      q1.MakeSameNeighborhood(q0);
      return q0.GetSlerped(s, q1).GetNormalized();
    }

    std::string InferAnimTypeFromFilename(const std::string &pathStr) {
      std::string lower = pathStr;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (lower.find("shortpass") != std::string::npos || lower.find("short_pass") != std::string::npos) return "shortpass";
      if (lower.find("longpass")  != std::string::npos || lower.find("long_pass")  != std::string::npos) return "longpass";
      if (lower.find("highpass")  != std::string::npos || lower.find("high_pass")  != std::string::npos) return "highpass";
      if (lower.find("shot")      != std::string::npos || lower.find("shoot")      != std::string::npos) return "shot";
      if (lower.find("trap")      != std::string::npos) return "trap";
      if (lower.find("ballcontrol") != std::string::npos || lower.find("dribble_touch") != std::string::npos) return "ballcontrol";
      if (lower.find("sliding")   != std::string::npos || lower.find("slide_tackle") != std::string::npos) return "sliding";
      if (lower.find("interfere") != std::string::npos || lower.find("tackle")     != std::string::npos) return "interfere";
      if (lower.find("deflect")   != std::string::npos) return "deflect";
      if (lower.find("catch")     != std::string::npos) return "catch";
      if (lower.find("trip")      != std::string::npos || lower.find("stumble")    != std::string::npos) return "trip";
      if (lower.find("special")   != std::string::npos || lower.find("celebrat")   != std::string::npos) return "special";
      return "movement";
    }

    float InferInPlaceSpeedFromFilename(const std::string &pathStr) {
      std::string lower = pathStr;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      if (lower.find("sprint") != std::string::npos) return 7.0f;
      if (lower.find("run") != std::string::npos || lower.find("jog") != std::string::npos) return 5.0f;
      if (lower.find("walk") != std::string::npos || lower.find("dribble") != std::string::npos) return 3.5f;
      return 0.0f;
    }

  } // namespace

  bool Animation::LoadGLTF(const std::string &filename) {
    name = filename;

    GLTFDocument doc;
    std::string err;
    if (!LoadGLTFDocument(filename, doc, err)) {
      Log(e_Error, "Animation", "LoadGLTF", err);
      return false;
    }
    if (doc.animations.empty() || doc.nodes.empty()) {
      Log(e_Error, "Animation", "LoadGLTF", "No animation clips found in " + filename);
      return false;
    }

    const GLTFAnimation &clip = doc.animations[0];

    // Decode all samplers of clip 0
    std::vector<DecodedSamplerTrack> tracks(clip.samplers.size());
    float maxDuration = 0.0f;
    for (size_t s = 0; s < clip.samplers.size(); ++s) {
      const GLTFAnimSampler &gs = clip.samplers[s];
      tracks[s].interpolation = gs.interpolation;
      if (gs.input >= 0 && gs.output >= 0 && gs.output < static_cast<int>(doc.accessors.size())) {
        ReadAccessorFloats(doc, gs.input, tracks[s].times);
        ReadAccessorFloats(doc, gs.output, tracks[s].values);
        tracks[s].numComp = GetAccessorComponentCount(doc.accessors[gs.output].type);
        if (!tracks[s].times.empty()) {
          maxDuration = std::max(maxDuration, tracks[s].times.back());
        }
      }
    }

    // Lossless fast-path for native GameplayFootball animations upgraded to glTF 2.0 (.glb)
    const bool isNativeSpace = clip.extras.GetBool("agy_native_space", false) ||
                               doc.extras.GetBool("agy_native_space", false);
    if (isNativeSpace) {
      // Ensure canonical NodeAnimation order: 0 = "player", 1..13 = "body".."right_ankle"
      auto ensureNodeTrack = [&](const std::string &trackName) -> NodeAnimation * {
        for (NodeAnimation *na : nodeAnimations) {
          if (na->nodeName == trackName) return na;
        }
        NodeAnimation *na = new NodeAnimation();
        na->nodeName = trackName;
        nodeAnimations.push_back(na);
        return na;
      };
      ensureNodeTrack("player");
      for (int j = 0; j < e_Joint_Count; ++j) {
        ensureNodeTrack(GetEngineJointName(j));
      }

      for (const GLTFAnimChannel &ch : clip.channels) {
        if (ch.targetNode < 0 || ch.targetNode >= static_cast<int>(doc.nodes.size())) continue;
        if (ch.sampler < 0 || ch.sampler >= static_cast<int>(tracks.size())) continue;
        const std::string &nodeName = doc.nodes[ch.targetNode].name;
        const DecodedSamplerTrack &tr = tracks[ch.sampler];

        if (ch.targetPath == "translation" && nodeName == "player" && tr.numComp >= 3) {
          size_t count = std::min(tr.times.size(), tr.values.size() / 3);
          for (size_t k = 0; k < count; ++k) {
            int frame = static_cast<int>(std::round(tr.times[k] * 100.0f));
            Vector3 pEng = GltfPosToEngine(tr.values[k * 3 + 0], tr.values[k * 3 + 1], tr.values[k * 3 + 2]);
            SetKeyFrame("player", frame, Quaternion(QUATERNION_IDENTITY), pEng);
          }
        } else if (ch.targetPath == "rotation" && nodeName != "player" && tr.numComp >= 4) {
          size_t count = std::min(tr.times.size(), tr.values.size() / 4);
          for (size_t k = 0; k < count; ++k) {
            int frame = static_cast<int>(std::round(tr.times[k] * 100.0f));
            // Preserve exact quaternion signs/values without re-normalizing away author sign convention
            Quaternion qEng(
              tr.values[k * 4 + 0],
              -tr.values[k * 4 + 2],
              tr.values[k * 4 + 1],
              tr.values[k * 4 + 3]
            );
            SetKeyFrame(nodeName, frame, qEng, Vector3(0));
          }
        }
      }

      int explicitFrames = clip.extras.GetInt("agy_frame_count", -1);
      if (explicitFrames > frameCount) {
        frameCount = explicitFrames;
      }

      std::string footStr = clip.extras.GetString("agy_current_foot", "right");
      currentFoot = (footStr == "left") ? e_Foot_Left : e_Foot_Right;

      // Restore exact XML metadata tree (and allow optional .xml sidecar override for modding)
      customData = std::shared_ptr<XMLTree>(new XMLTree());
      std::filesystem::path gltfPath(filename);
      std::string sidecar1 = filename + ".xml";
      std::string sidecar2 = gltfPath.replace_extension(".xml").generic_string();
      std::error_code ec;
      XMLLoader xmlLoader;
      if (std::filesystem::exists(sidecar1, ec)) {
        *customData = xmlLoader.LoadFile(sidecar1);
      } else if (std::filesystem::exists(sidecar2, ec)) {
        *customData = xmlLoader.LoadFile(sidecar2);
      } else {
        std::string rawXml = clip.extras.GetString("agy_xml_data", "");
        if (!rawXml.empty()) {
          *customData = xmlLoader.Load(rawXml);
        }
      }

      // Normalize directional vectors & populate variableCache identically to Animation::Load
      for (const char *dirKey : { "bumpdirection", "balldirection", "incomingballdirection" }) {
        auto it = customData->children.find(dirKey);
        if (it != customData->children.end()) {
          Vector3 dirVec = GetVectorFromString(it->second.value);
          if (dirVec.GetLength() > 0.0f) dirVec.Normalize();
          it->second.value = real_to_str(dirVec.coords[0]) + "," + real_to_str(dirVec.coords[1]) + "," + real_to_str(dirVec.coords[2]);
        }
      }
      for (auto iter = customData->children.begin(); iter != customData->children.end(); ++iter) {
        char *varName = new char[256];
        std::strncpy(varName, iter->first.c_str(), 255);
        varName[255] = '\0';
        variableCache.insert(std::pair<const char *, std::string>(varName, iter->second.value));
      }
      cache_AnimType = GetVariable("type");

      // Restore exact FootballAnimationExtension keyframes
      auto fbExtIt = extensions.find("football");
      const GLTFJsonValue &touchesJson = clip.extras.Get("agy_football_touches");
      if (fbExtIt != extensions.end() && touchesJson.IsArray()) {
        auto fbExt = std::static_pointer_cast<FootballAnimationExtension>(fbExtIt->second);
        for (const GLTFJsonValue &tk : touchesJson.arrVal) {
          if (tk.IsArray() && tk.arrVal.size() >= 9) {
            int tf = static_cast<int>(std::round(tk.arrVal[0].numVal));
            Vector3 tp(
              static_cast<float>(tk.arrVal[1].numVal),
              static_cast<float>(tk.arrVal[2].numVal),
              static_cast<float>(tk.arrVal[3].numVal)
            );
            Quaternion to(
              static_cast<float>(tk.arrVal[4].numVal),
              static_cast<float>(tk.arrVal[5].numVal),
              static_cast<float>(tk.arrVal[6].numVal),
              static_cast<float>(tk.arrVal[7].numVal)
            );
            float power = static_cast<float>(tk.arrVal[8].numVal);
            fbExt->SetKeyFrame(tf, to, tp, power);
          }
        }
      }

      DirtyCache();
      return true;
    }

    // Bind-pose limb alignment (e.g. Mixamo T-Pose -> GameplayFootball straight.anim.util pose)
    Quaternion bindAlignRot[e_Joint_Count];
    int primaryNodeForJoint[e_Joint_Count];
    float rigUnitScale = 1.0f;
    ComputeBindPoseLimbAlignment(doc, bindAlignRot, primaryNodeForJoint, rigUnitScale);

    // For multi-bone Spine (Spine -> Spine1 -> Spine2) and Neck/Head, pick distal spine/head node for cumulative orientation
    int orientNodeForJoint[e_Joint_Count];
    for (int j = 0; j < e_Joint_Count; ++j) orientNodeForJoint[j] = primaryNodeForJoint[j];
    for (size_t i = 0; i < doc.nodes.size(); ++i) {
      int j = ResolveNodeToEngineJointIndex(doc, static_cast<int>(i));
      if (j == e_Joint_Middle || j == e_Joint_Neck) {
        // Deeper spine/head node (but skip leaf end-effectors with no children if another exists, unless it's "head" or "neck")
        std::string lname = doc.nodes[i].name;
        std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
        if (lname.find("end") == std::string::npos && lname.find("eye") == std::string::npos &&
            lname.find("jaw") == std::string::npos) {
          orientNodeForJoint[j] = static_cast<int>(i);
        }
      }
    }

    // Map per-node channels (translation / rotation)
    std::vector<int> nodeTransSampler(doc.nodes.size(), -1);
    std::vector<int> nodeRotSampler(doc.nodes.size(), -1);
    int rootMotionNode = primaryNodeForJoint[e_Joint_Body];

    for (const GLTFAnimChannel &ch : clip.channels) {
      if (ch.targetNode < 0 || ch.targetNode >= static_cast<int>(doc.nodes.size())) continue;
      if (ch.sampler < 0 || ch.sampler >= static_cast<int>(tracks.size())) continue;
      if (ch.targetPath == "translation") {
        nodeTransSampler[ch.targetNode] = ch.sampler;
        if (rootMotionNode < 0 && IsRootMotionBoneName(doc.nodes[ch.targetNode].name)) {
          rootMotionNode = ch.targetNode;
        }
      } else if (ch.targetPath == "rotation") {
        nodeRotSampler[ch.targetNode] = ch.sampler;
      }
    }

    // Resample at GameplayFootball's 100 Hz (10ms per frame)
    int totalFrames = std::max(2, static_cast<int>(std::round(maxDuration * 100.0f)) + 1);

    // Pre-create canonical node tracks in exact engine order: 0 = "player", 1..13 = "body".."right_ankle"
    SetKeyFrame("player", 0, Quaternion(QUATERNION_IDENTITY), Vector3(0));
    for (int j = 0; j < e_Joint_Count; ++j) {
      SetKeyFrame(GetEngineJointName(j), 0, Quaternion(QUATERNION_IDENTITY), Vector3(0));
    }

    Vector3 startRootXY(0);
    float restHipZ = (rootMotionNode >= 0) ? (doc.nodes[rootMotionNode].engineGlobalPos.coords[2] * rigUnitScale) : 0.96f;

    std::vector<Vector3> sampledRootPositions(totalFrames, Vector3(0));
    float minFootDeltaX = 0.0f, maxFootDeltaYLeft = 0.0f, maxFootDeltaYRight = 0.0f;
    (void)minFootDeltaX;

    // Temporary FK buffers per frame
    std::vector<Quaternion> animLocalRotEng(doc.nodes.size());
    std::vector<Vector3> animLocalPosEng(doc.nodes.size());
    std::vector<Quaternion> animGlobalRotEng(doc.nodes.size());
    std::vector<Vector3> animGlobalPosEng(doc.nodes.size());
    std::vector<uint8_t> fkState(doc.nodes.size(), 0);

    auto solveAnimFK = [&](auto &self, int idx) -> void {
      if (idx < 0 || idx >= static_cast<int>(doc.nodes.size())) return;
      if (fkState[idx] == 2 || fkState[idx] == 1) return;
      fkState[idx] = 1;
      const GLTFNode &n = doc.nodes[idx];
      if (n.parent >= 0 && n.parent < static_cast<int>(doc.nodes.size())) {
        self(self, n.parent);
        Vector3 scaledLocal(
          animLocalPosEng[idx].coords[0] * doc.nodes[n.parent].engineGlobalScale.coords[0],
          animLocalPosEng[idx].coords[1] * doc.nodes[n.parent].engineGlobalScale.coords[1],
          animLocalPosEng[idx].coords[2] * doc.nodes[n.parent].engineGlobalScale.coords[2]
        );
        animGlobalPosEng[idx] = animGlobalPosEng[n.parent] + (animGlobalRotEng[n.parent] * scaledLocal);
        animGlobalRotEng[idx] = (animGlobalRotEng[n.parent] * animLocalRotEng[idx]).GetNormalized();
      } else {
        animGlobalPosEng[idx] = animLocalPosEng[idx];
        animGlobalRotEng[idx] = animLocalRotEng[idx];
      }
      fkState[idx] = 2;
    };

    Quaternion prevOutLocal[e_Joint_Count];
    for (int j = 0; j < e_Joint_Count; ++j) prevOutLocal[j] = Quaternion(QUATERNION_IDENTITY);

    Vector3 firstLeftAnkle(0), firstRightAnkle(0);

    for (int f = 0; f < totalFrames; ++f) {
      float t = static_cast<float>(f) * 0.01f;

      for (size_t i = 0; i < doc.nodes.size(); ++i) {
        const GLTFNode &n = doc.nodes[i];
        if (nodeTransSampler[i] >= 0) {
          Vector3 tg = SampleGLTFVec3(tracks[nodeTransSampler[i]], t, n.translation);
          animLocalPosEng[i] = GltfPosToEngine(tg);
        } else {
          animLocalPosEng[i] = n.engineLocalPos;
        }

        if (nodeRotSampler[i] >= 0) {
          Quaternion rg = SampleGLTFQuat(tracks[nodeRotSampler[i]], t, n.rotation);
          animLocalRotEng[i] = GltfQuatToEngine(rg.elements[0], rg.elements[1], rg.elements[2], rg.elements[3]);
        } else {
          animLocalRotEng[i] = n.engineLocalRot;
        }
      }

      std::fill(fkState.begin(), fkState.end(), 0);
      for (size_t i = 0; i < doc.nodes.size(); ++i) {
        solveAnimFK(solveAnimFK, static_cast<int>(i));
      }

      // Root motion extraction ("player" node)
      Vector3 rootPos(0);
      if (rootMotionNode >= 0) {
        Vector3 worldHip = animGlobalPosEng[rootMotionNode] * rigUnitScale;
        if (f == 0) {
          startRootXY = Vector3(worldHip.coords[0], worldHip.coords[1], 0.0f);
        }
        rootPos.coords[0] = worldHip.coords[0] - startRootXY.coords[0];
        rootPos.coords[1] = worldHip.coords[1] - startRootXY.coords[1];
        rootPos.coords[2] = clamp(worldHip.coords[2] - restHipZ, -0.85f, 1.20f);
      }
      sampledRootPositions[f] = rootPos;

      // Detect which foot moves forward first in early frames (for currentFoot)
      if (primaryNodeForJoint[e_Joint_LeftAnkle] >= 0 && primaryNodeForJoint[e_Joint_RightAnkle] >= 0) {
        Vector3 la = animGlobalPosEng[primaryNodeForJoint[e_Joint_LeftAnkle]] * rigUnitScale;
        Vector3 ra = animGlobalPosEng[primaryNodeForJoint[e_Joint_RightAnkle]] * rigUnitScale;
        if (f == 0) {
          firstLeftAnkle = la;
          firstRightAnkle = ra;
        } else if (f <= std::min(15, totalFrames - 1)) {
          maxFootDeltaYLeft = std::max(maxFootDeltaYLeft, (la - firstLeftAnkle).Get2D().GetLength());
          maxFootDeltaYRight = std::max(maxFootDeltaYRight, (ra - firstRightAnkle).Get2D().GetLength());
        }
      }

      // Compute retargeted world-space orientations Q_global_eng[j] for all 13 joints
      Quaternion qGlobalEng[e_Joint_Count];
      for (int j = 0; j < e_Joint_Count; ++j) {
        int kNode = orientNodeForJoint[j];
        if (kNode >= 0) {
          const Quaternion &rAnimGlobal = animGlobalRotEng[kNode];
          Quaternion rRestGlobalInv = doc.nodes[kNode].engineGlobalRot.GetInverse();
          Quaternion rAlignInv = bindAlignRot[j].GetInverse();
          qGlobalEng[j] = (rAnimGlobal * rRestGlobalInv * rAlignInv).GetNormalized();
        } else {
          int pJoint = GetEngineJointParent(j);
          qGlobalEng[j] = (pJoint >= 0) ? qGlobalEng[pJoint] : Quaternion(QUATERNION_IDENTITY);
        }
      }

      // Convert world-space Q_global_eng[j] into parent-local rotations for player.object
      for (int j = 0; j < e_Joint_Count; ++j) {
        int pJoint = GetEngineJointParent(j);
        Quaternion qLocal = (pJoint >= 0)
          ? (qGlobalEng[pJoint].GetInverse() * qGlobalEng[j]).GetNormalized()
          : qGlobalEng[j];
        if (f > 0) qLocal.MakeSameNeighborhood(prevOutLocal[j]);
        prevOutLocal[j] = qLocal;
        SetKeyFrame(GetEngineJointName(j), f, qLocal, Vector3(0));
      }
    }

    currentFoot = (maxFootDeltaYLeft > maxFootDeltaYRight * 1.15f) ? e_Foot_Left : e_Foot_Right;

    // Optional sidecar XML metadata (<filename>.xml or <stem>.xml) + glTF extras
    customData = std::shared_ptr<XMLTree>(new XMLTree());
    std::filesystem::path gltfPath(filename);
    std::string sidecar1 = filename + ".xml";
    std::string sidecar2 = gltfPath.replace_extension(".xml").generic_string();
    std::error_code ec;
    if (std::filesystem::exists(sidecar1, ec)) {
      XMLLoader xmlLoader;
      *customData = xmlLoader.LoadFile(sidecar1);
    } else if (std::filesystem::exists(sidecar2, ec)) {
      XMLLoader xmlLoader;
      *customData = xmlLoader.LoadFile(sidecar2);
    }

    auto setDefaultMetaIfMissing = [&](const std::string &key, const std::string &val) {
      if (customData->children.find(key) == customData->children.end() && !val.empty()) {
        XMLTree leaf;
        leaf.value = val;
        customData->children.insert(std::make_pair(key, leaf));
      }
    };

    // Merge string/number fields from glTF animation extras & root extras
    auto mergeJsonExtras = [&](const GLTFJsonValue &ext) {
      if (!ext.IsObject()) return;
      for (const auto &kv : ext.objVal) {
        if (kv.second.IsString()) {
          setDefaultMetaIfMissing(kv.first, kv.second.strVal);
        } else if (kv.second.IsNumber()) {
          setDefaultMetaIfMissing(kv.first, real_to_str(static_cast<float>(kv.second.numVal)));
        } else if (kv.second.IsBool()) {
          setDefaultMetaIfMissing(kv.first, kv.second.boolVal ? "true" : "false");
        }
      }
    };
    mergeJsonExtras(clip.extras);
    mergeJsonExtras(doc.extras);

    setDefaultMetaIfMissing("type", InferAnimTypeFromFilename(filename));

    // Check if clip was exported "In Place" (zero XY displacement) while representing locomotion
    float totalXYDisp = (sampledRootPositions.back() - sampledRootPositions.front()).Get2D().GetLength();
    float overrideSpeed = 0.0f;
    auto velMetaIt = customData->children.find("velocity");
    if (velMetaIt != customData->children.end()) {
      overrideSpeed = static_cast<float>( std::atof(velMetaIt->second.value.c_str()) );
    } else if (totalXYDisp < 0.05f) {
      overrideSpeed = InferInPlaceSpeedFromFilename(filename);
    }

    for (int f = 0; f < totalFrames; ++f) {
      Vector3 p = sampledRootPositions[f];
      if (overrideSpeed > 0.1f && totalXYDisp < 0.05f) {
        p.coords[0] = 0.0f;
        p.coords[1] = -overrideSpeed * (static_cast<float>(f) * 0.01f);
      }
      SetKeyFrame("player", f, Quaternion(QUATERNION_IDENTITY), p);
    }

    // Populate variableCache from customData (matching Animation::Load)
    for (auto iter = customData->children.begin(); iter != customData->children.end(); ++iter) {
      if (iter->first == "bumpdirection" || iter->first == "balldirection" || iter->first == "incomingballdirection") {
        Vector3 dirVec = GetVectorFromString(iter->second.value);
        if (dirVec.GetLength() > 0.0f) dirVec.Normalize();
        iter->second.value = real_to_str(dirVec.coords[0]) + "," + real_to_str(dirVec.coords[1]) + "," + real_to_str(dirVec.coords[2]);
      }
      char *varName = new char[256];
      std::strncpy(varName, iter->first.c_str(), 255);
      varName[255] = '\0';
      variableCache.insert(std::pair<const char*, std::string>(varName, iter->second.value));
    }

    cache_AnimType = GetVariable("type");
    if (cache_AnimType.empty()) {
      SetVariable("type", "movement");
      cache_AnimType = "movement";
    }

    // Optional ball touch synthesis for ball-action animations if not explicitly provided
    auto fbExtIt = extensions.find("football");
    if (fbExtIt != extensions.end()) {
      auto fbExt = std::static_pointer_cast<FootballAnimationExtension>(fbExtIt->second);
      auto touchFrameIt = customData->children.find("touchframe");
      auto touchPosIt = customData->children.find("touchpos");
      if (touchFrameIt != customData->children.end()) {
        int tf = clamp(std::atoi(touchFrameIt->second.value.c_str()), 0, totalFrames - 1);
        Vector3 tp = (touchPosIt != customData->children.end())
          ? GetVectorFromString(touchPosIt->second.value)
          : (sampledRootPositions[tf].Get2D() + Vector3(0.0f, -0.35f, 0.11f));
        fbExt->SetKeyFrame(tf, Quaternion(QUATERNION_IDENTITY), tp, 1.0f);
      } else if (cache_AnimType != "movement" && cache_AnimType != "trip" && cache_AnimType != "special") {
        int midFrame = totalFrames / 2;
        Vector3 defaultTouch = sampledRootPositions[midFrame].Get2D() + Vector3(0.0f, -0.35f, 0.11f);
        fbExt->SetKeyFrame(midFrame, Quaternion(QUATERNION_IDENTITY), defaultTouch, 1.0f);
      }
    }

    DirtyCache();

    // Align starting facing/movement direction to GameplayFootball's -Y forward axis
    if (GetIncomingVelocity() >= 1.8f) {
      radian moveAngle = FixAngle(GetIncomingMovement().GetAngle2D(), true);
      if (std::fabs(moveAngle) > 1e-4f) {
        for (auto &kf : nodeAnimations.at(0)->animation) {
          kf.second.position.Rotate2D(-moveAngle);
        }
        Quaternion zRot;
        zRot.SetAngleAxis(-moveAngle, Vector3(0, 0, 1));
        for (auto &kf : nodeAnimations.at(1)->animation) {
          kf.second.orientation = (zRot * kf.second.orientation).GetNormalized();
        }
        for (auto &extKv : extensions) {
          extKv.second->Rotate2D(-moveAngle);
        }
        DirtyCache();
      }
    } else {
      ConvertToStartFacingForwardIfIdle();
    }

    Log(e_Notice, "Animation", "LoadGLTF",
        "Loaded glTF animation '" + filename + "' (" + int_to_str(totalFrames) + " frames, type=" + cache_AnimType + ")");
    return true;
  }

  void Animation::Load(const std::string &filename) {
    std::string ext = get_file_extension(filename);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == "glb" || ext == "gltf") {
      LoadGLTF(filename);
      return;
    }

    name = filename;

    std::vector<std::string> file;
    file_to_vector(filename, file);

    std::vector < std::vector<std::string> > tokenizedFile;
    int lastLine = 0;
    for (unsigned int i = 0; i < file.size(); i++) {
      std::vector<std::string> tokenizedLine;
      tokenize(file.at(i), tokenizedLine, ",");
      if (tokenizedLine.at(0) == "extension" || tokenizedLine.at(0).substr(0, 1) == "<") break;
      tokenizedFile.push_back(tokenizedLine);
      lastLine = i + 1;
    }

    LoadData(tokenizedFile);

    // cache extension data
    std::vector < std::vector <std::string> > tokenizedLines;

    for (unsigned int i = lastLine; i < file.size(); i++) {
      std::vector<std::string> tokenizedLine;
      tokenize(file.at(i), tokenizedLine, ",");
      std::map < std::string, std::shared_ptr<AnimationExtension> >::iterator extensionIter;
      if (tokenizedLine.at(0) == "extension") {
        tokenizedLines.push_back(tokenizedLine);
      } else {
        break;
      }
      lastLine = i + 1;
    }

    // additional xml data
    std::string xmlData;
    for (unsigned int i = lastLine; i < file.size(); i++) {
      xmlData.append(file.at(i));
    }
    XMLLoader xmlLoader;
    customData = std::shared_ptr<XMLTree>(new XMLTree(xmlLoader.Load(xmlData)));

    // load extension data
    for (unsigned int i = 0; i < tokenizedLines.size(); i++) {
      std::map < std::string, std::shared_ptr<AnimationExtension> >::iterator extensionIter;
      extensionIter = extensions.find(tokenizedLines.at(i).at(1));
      if (extensionIter != extensions.end()) (*extensionIter).second->Load(tokenizedLines.at(i));
    }

    std::multimap<std::string, XMLTree>::iterator iter = customData->children.find("bumpdirection");
    if (iter != customData->children.end()) {
      const XMLTree &tree = iter->second;
      Vector3 bumpDirection = GetVectorFromString(tree.value);
      if (bumpDirection.GetLength() > 0) bumpDirection.Normalize();
      iter->second.value = real_to_str(bumpDirection.coords[0]) + "," + real_to_str(bumpDirection.coords[1]) + "," + real_to_str(bumpDirection.coords[2]);
    }

    iter = customData->children.find("balldirection");
    if (iter != customData->children.end()) {
      const XMLTree &tree = iter->second;
      Vector3 ballDirection = GetVectorFromString(tree.value);
      if (ballDirection.GetLength() > 0) ballDirection.Normalize();
      iter->second.value = real_to_str(ballDirection.coords[0]) + "," + real_to_str(ballDirection.coords[1]) + "," + real_to_str(ballDirection.coords[2]);
    }

    iter = customData->children.find("incomingballdirection");
    if (iter != customData->children.end()) {
      const XMLTree &tree = iter->second;
      Vector3 incomingBallDirection = GetVectorFromString(tree.value);
      if (incomingBallDirection.GetLength() > 0) incomingBallDirection.Normalize();
      iter->second.value = real_to_str(incomingBallDirection.coords[0]) + "," + real_to_str(incomingBallDirection.coords[1]) + "," + real_to_str(incomingBallDirection.coords[2]);
    }

    // create variable cache
    iter = customData->children.begin();
    while (iter != customData->children.end()) {
      char *varName = new char[256];
      strcpy(varName, (*iter).first.c_str());
      std::string varData = (*iter).second.value;
      //printf("varname: %s, vardata: %s\n", varName, varData.c_str());

      variableCache.insert(std::pair<const char*, std::string>(varName, varData));

      iter++;
    }

    cache_AnimType = variableCache.find("type")->second;

    ConvertToStartFacingForwardIfIdle();

  }

  void Animation::Save(const std::string &filename) {
    std::vector<std::string> fileData;
    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line;
      line = nodeAnimations.at(i)->nodeName + ",";
      std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
      while (animIter != nodeAnimations.at(i)->animation.end()) {
        line.append(int_to_str(animIter->first) + ","); // frame number
        if (nodeAnimations.at(i)->nodeName != "player") {
          line.append(real_to_str(animIter->second.orientation.elements[0]) + ","); // X element
          line.append(real_to_str(animIter->second.orientation.elements[1]) + ","); // Y element
          line.append(real_to_str(animIter->second.orientation.elements[2]) + ","); // Z element
          line.append(real_to_str(animIter->second.orientation.elements[3]) + ","); // W element
        } else {
          line.append(real_to_str(animIter->second.position.coords[0]) + ","); // X pos
          line.append(real_to_str(animIter->second.position.coords[1]) + ","); // Y pos
          line.append(real_to_str(animIter->second.position.coords[2]) + ","); // Z pos
        }
        animIter++;
      }
      line = line.substr(0, line.length() - 1);
      fileData.push_back(line);
    }

    FILE *file;
    file = fopen(filename.c_str(), "w");
    if (!file) {
      Log(e_Error, "Animation", "Save", "Could not open file " + filename + " for writing");
      return;
    }
    for (int i = 0; i < (signed int)fileData.size(); i++) {
      fprintf(file, "%s\n", fileData.at(i).c_str());
    }

    std::map < std::string, std::shared_ptr<AnimationExtension> >::iterator extensionIter = extensions.begin();
    while (extensionIter != extensions.end()) {
      (*extensionIter).second->Save(file);
      extensionIter++;
    }

    XMLLoader loader;
    fprintf(file, "%s", loader.GetSource((*customData)).c_str());

    fclose(file);
  }

  void Animation::Mirror() {
    name.append("_mirror");
    (currentFoot == e_Foot_Right) ? currentFoot = e_Foot_Left : currentFoot = e_Foot_Right;

    for (unsigned int i = 0; i < nodeAnimations.size(); i++) {

      if (nodeAnimations.at(i)->nodeName.substr(0, 4) == "left") {
        // find counterpart
        std::string needle = nodeAnimations.at(i)->nodeName;
        needle = needle.replace(0, 4, "right");

        for (unsigned int j = 0; j < nodeAnimations.size(); j++) {
          if (nodeAnimations.at(j)->nodeName.compare(needle) == 0) {
            // swap
            std::map<int, KeyFrame> tmp = nodeAnimations.at(i)->animation;
            nodeAnimations.at(i)->animation = nodeAnimations.at(j)->animation;
            nodeAnimations.at(j)->animation = tmp;
            break;
          }
        }

      }

    }

    for (unsigned int i = 0; i < nodeAnimations.size(); i++) {

      std::map<int, KeyFrame>::iterator keyIter = nodeAnimations.at(i)->animation.begin();
      while (keyIter != nodeAnimations.at(i)->animation.end()) {
        if (i == 0) {
          keyIter->second.position.coords[0] = -keyIter->second.position.coords[0];
        } else {
          keyIter->second.orientation.elements[1] = -keyIter->second.orientation.elements[1];
          keyIter->second.orientation.elements[2] = -keyIter->second.orientation.elements[2];
        }
        keyIter++;
      }

    }

    // extensions!
    std::map < std::string, std::shared_ptr<AnimationExtension> >::iterator extensionIter = extensions.begin();
    while (extensionIter != extensions.end()) {
      extensionIter->second->Mirror();
      extensionIter++;
    }

    // variables
    std::map<const char*, std::string>::iterator varIter = variableCache.begin();
    while (varIter != variableCache.end()) {
      std::string &varData = varIter->second;
      if (varData.substr(0, 4) == "left") {
        varData = varData.replace(0, 4, "right");
      } else if (varData.substr(0, 5) == "right") {
        varData = varData.replace(0, 5, "left");
      }
      varIter++;
    }

    Vector3 newBallDirVec = GetVariable("balldirection") != "" ? GetVectorFromString(GetVariable("balldirection")) * Vector3(-1, 1, 1) : 0;
    Vector3 newIncomingBallDirVec = GetVariable("incomingballdirection") != "" ? GetVectorFromString(GetVariable("incomingballdirection")) * Vector3(-1, 1, 1) : 0;
    Vector3 newBumpDirVec = GetVariable("bumpdirection") != "" ? GetVectorFromString(GetVariable("bumpdirection")) * Vector3(-1, 1, 1) : 0;
    SetVariable("balldirection", GetStringFromVector(newBallDirVec));
    SetVariable("incomingballdirection", GetStringFromVector(newIncomingBallDirVec));
    SetVariable("bumpdirection", GetStringFromVector(newBumpDirVec));

    DirtyCache();
  }

  std::string Animation::GetName() const {
    return name;
  }

  void Animation::AddExtension(const std::string &name, std::shared_ptr<AnimationExtension> extension) {
    extensions.insert(std::pair < std::string, std::shared_ptr<AnimationExtension> >(name, extension));
  }

  std::shared_ptr<AnimationExtension> Animation::GetExtension(const std::string &name) {
    return extensions.find(name)->second;
  }

  const std::string &Animation::GetVariable(const char *name) const {

    std::map<const char*, std::string>::const_iterator iter = variableCache.find(name);
    //printf("looking for %s.. %s\n", name, variableCache.begin()->first);
    if (iter != variableCache.end()) {
      //printf("found %s\n", iter->second.c_str());
      return iter->second;
    } else {
      //printf("found nothing\n");
      return emptyString;
    }
  }

  void Animation::SetVariable(const std::string &name, const std::string &value) {
    std::multimap<std::string, XMLTree>::iterator iter = customData->children.find(name);
    if (iter != customData->children.end()) {
      XMLTree &tree = iter->second;
      tree.value = value;
    }

    // flat list for speed, i guess, i should start documenting stuff earlier
    std::map<const char*, std::string>::iterator iter2 = variableCache.find(name.c_str());
    if (iter2 != variableCache.end()) {
      iter2->second = value;
    } else {
      char *varName = new char[256];
      strcpy(varName, name.c_str());
      variableCache.insert(std::pair<const char*, std::string>(varName, value));
    }
  }

  std::shared_ptr<XMLTree> Animation::GetCustomData() {
    return customData;
  }

  void Animation::Hax() {
/*
    // outgoing ball angle == outgoing player angle
    Vector3 position;
    int frame;
    bool touch = blunted::static_pointer_cast<FootballAnimationExtension>(GetExtension("football"))->GetFirstTouch(position, frame);
    if (touch) {
      Vector3 angles, position;
      float power;
      GetExtension("football")->GetKeyFrame(frame, angles, position, power);
      //angles.coords[2] = ModulateIntoRange(-pi, pi, GetAngle());
      power = 1;//GetOutgoingVelocity() * 0.2;

      if (GetVariable("type") == "shortpass") {
        power = 2;//GetOutgoingVelocity() * 0.4;
      }


      //position = Vector3(0.0, -0.1 - 0.012 * GetOutgoingMovement().GetLength() * frame, 0);


      //if (position.GetAngle2D() + 0.5 * pi < 3.0)
      //position.Rotate2D(-(position.GetAngle2D() + 0.5 * pi));
      //position.Print();
      //position = Vector3(0, -0.5, 0);
      GetExtension("football")->SetKeyFrame(frame, angles, position, power);
      //printf("%f\n", power);
    }
*/

/*
      XMLTree tree;
      tree.value = "movement";
      customData->children.insert(std::pair<std::string, XMLTree>("type", tree));
*/


/*
    for (int i = 0; i < nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->node->GetName();
      printf("%s\n", line.c_str());
      if (line == "player") nodeAnimations.at(i)->node->SetName("body");
      if (line == "player node") nodeAnimations.at(i)->node->SetName("player");
    }
    printf("\n\n");
*/

/*
  nodeAnimations.at(0)->node->SetName("player");
  nodeAnimations.at(1)->node->SetName("body");
*/

// put player rotation into body
/*
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(0)->animation.begin();
        while (animIter != nodeAnimations.at(0)->animation.end()) {
          Vector3 angles = animIter->second.angles;
          animIter->second.angles = Vector3(0);
          SetKeyFrame(nodeAnimations.at(1)->node, animIter->first, angles, Vector3(0));
          animIter++;
        }
*/

// exaggerate middle forward bending
/*
    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->nodeName;
      if (line == "middle") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          animIter->second.angles.coords[0] += (pi * 2) / 360.0 * 2;
          animIter++;
        }
      }
    }
*/

    // exaggerate body lending
    /*
    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->nodeName;
      if (line == "body") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          radian rotXa, rotYa, rotZa;
          animIter->second.orientation.GetAngles(rotXa, rotYa, rotZa);

          rotXa *= 1.4;
          rotYa *= 1.4;

          Quaternion rotX, rotY, rotZ, quat;
          rotX.SetAngleAxis(rotXa, Vector3(1, 0, 0));
          rotY.SetAngleAxis(rotYa, Vector3(0, 1, 0));
          rotZ.SetAngleAxis(rotZa, Vector3(0, 0, 1));
          quat = rotX * rotY * rotZ;

          animIter->second.orientation = quat;

          animIter++;
        }
      }
    }
    */

// bend knees/hips more - lower player stance
/*
    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->nodeName;
      if (line == "left_thigh" || line == "right_thigh") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          radian rotXa, rotYa, rotZa;
          animIter->second.orientation.GetAngles(rotXa, rotYa, rotZa);

          rotXa -= 0.04;
          rotXa *= 1.1;

          Quaternion rotX, rotY, rotZ, quat;
          rotX.SetAngleAxis(rotXa, Vector3(1, 0, 0));
          rotY.SetAngleAxis(rotYa, Vector3(0, 1, 0));
          rotZ.SetAngleAxis(rotZa, Vector3(0, 0, 1));
          quat = rotX * rotY * rotZ;

          animIter->second.orientation = quat;

          animIter++;
        }
      }
      if (line == "left_knee" || line == "right_knee") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          radian rotXa, rotYa, rotZa;
          animIter->second.orientation.GetAngles(rotXa, rotYa, rotZa);

          rotXa += 0.08;
          rotXa *= 1.2;

          Quaternion rotX, rotY, rotZ, quat;
          rotX.SetAngleAxis(rotXa, Vector3(1, 0, 0));
          rotY.SetAngleAxis(rotYa, Vector3(0, 1, 0));
          rotZ.SetAngleAxis(rotZa, Vector3(0, 0, 1));
          quat = rotX * rotY * rotZ;

          animIter->second.orientation = quat;

          animIter++;
        }
      }
      if (line == "player") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {

          animIter->second.position.coords[2] -= 0.01;

          animIter++;
        }
      }
    }
*/

// exaggerate everything

    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->nodeName;
      if (line != "body") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          radian rotXa, rotYa, rotZa;
          animIter->second.orientation.GetAngles(rotXa, rotYa, rotZa);

          rotXa *= 1.1;
          rotYa *= 1.1;

          Quaternion rotX, rotY, rotZ, quat;
          rotX.SetAngleAxis(rotXa, Vector3(1, 0, 0));
          rotY.SetAngleAxis(rotYa, Vector3(0, 1, 0));
          rotZ.SetAngleAxis(rotZa, Vector3(0, 0, 1));
          quat = rotX * rotY * rotZ;

          animIter->second.orientation = quat;

          animIter++;
        }
      }
    }


// bend body/middle more
/*
    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->nodeName;
*/
/*
      if (line == "body") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          radian rotXa, rotYa, rotZa;
          animIter->second.orientation.GetAngles(rotXa, rotYa, rotZa);

          rotXa *= 1.4;
          rotYa *= 1.4;

          Quaternion rotX, rotY, rotZ, quat;
          rotX.SetAngleAxis(rotXa, Vector3(1, 0, 0));
          rotY.SetAngleAxis(rotYa, Vector3(0, 1, 0));
          rotZ.SetAngleAxis(rotZa, Vector3(0, 0, 1));
          quat = rotX * rotY * rotZ;

          animIter->second.orientation = quat;

          animIter++;
        }
      }
      */
/*
      if (line == "middle") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          radian rotXa, rotYa, rotZa;
          animIter->second.orientation.GetAngles(rotXa, rotYa, rotZa);

          rotXa -= 0.2;
          //rotYa -= 0.5;
          rotXa *= 1.2;
          //rotYa *= 1.4;

          Quaternion rotX, rotY, rotZ, quat;
          rotX.SetAngleAxis(rotXa, Vector3(1, 0, 0));
          rotY.SetAngleAxis(rotYa, Vector3(0, 1, 0));
          rotZ.SetAngleAxis(rotZa, Vector3(0, 0, 1));
          quat = rotX * rotY * rotZ;

          animIter->second.orientation = quat;

          animIter++;
        }
      }
    }
*/


// bend arms more
/*
    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->nodeName;
      if (line == "left_shoulder" || line == "right_shoulder") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          radian rotXa, rotYa, rotZa;
          animIter->second.orientation.GetAngles(rotXa, rotYa, rotZa);

          rotXa += 0.25f;
          //rotXa *= 1.4;

          Quaternion rotX, rotY, rotZ, quat;
          rotX.SetAngleAxis(rotXa, Vector3(1, 0, 0));
          rotY.SetAngleAxis(rotYa, Vector3(0, 1, 0));
          rotZ.SetAngleAxis(rotZa, Vector3(0, 0, 1));
          quat = rotX * rotY * rotZ;

          animIter->second.orientation = quat;

          animIter++;
        }
      }
*/
      /*
      if (line == "left_elbow" || line == "right_elbow") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          radian rotXa, rotYa, rotZa;
          animIter->second.orientation.GetAngles(rotXa, rotYa, rotZa);

          rotXa += 0.28;
          rotXa *= 1.4;

          Quaternion rotX, rotY, rotZ, quat;
          rotX.SetAngleAxis(rotXa, Vector3(1, 0, 0));
          rotY.SetAngleAxis(rotYa, Vector3(0, 1, 0));
          rotZ.SetAngleAxis(rotZa, Vector3(0, 0, 1));
          quat = rotX * rotY * rotZ;

          animIter->second.orientation = quat;

          animIter++;
        }
      }
    */
//    }

// faster movement
/*
    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->nodeName;
      if (line == "player") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {

          animIter->second.position.coords[0] *= 1.428;
          animIter->second.position.coords[1] *= 1.428;

          animIter++;
        }
      }
    }
*/

// heighten body node 0.74
/*
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(1)->animation.begin();
        while (animIter != nodeAnimations.at(1)->animation.end()) {
          Vector3 angles = animIter->second.angles;
          animIter->second.position += Vector3(0, 0, 0.74);
          animIter++;
        }
*/

// exaggerate body angle in bends
/*
    radian angle = GetAngle();
    if (angle > 0.24 * pi) angle = 0.24 * pi;
    float vel = (GetIncomingMovement().GetLength() + GetOutgoingMovement().GetLength() * 2) / 3.0;
    angle *= vel;

        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(1)->animation.begin();
        while (animIter != nodeAnimations.at(1)->animation.end()) {
          animIter->second.angles.coords[1] += angle * 3;
          animIter++;
        }
*/

// arms higher
    /*
    for (int i = 0; i < (signed int)nodeAnimations.size(); i++) {
      std::string line = nodeAnimations.at(i)->nodeName;
      if (line == "left_shoulder" || line == "right_shoulder") {
        std::map<int, KeyFrame>::iterator animIter = nodeAnimations.at(i)->animation.begin();
        while (animIter != nodeAnimations.at(i)->animation.end()) {
          float offset;
          if (line == "left_shoulder") offset = -0.05 * pi; else offset = 0.05 * pi;
          //animIter->second.angles.coords[1] += offset;
          animIter->second.angles.coords[1] += offset;
          animIter++;
        }
      }
    }
    */

  }

}
