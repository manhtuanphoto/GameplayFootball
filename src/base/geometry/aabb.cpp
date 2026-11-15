// written by bastiaan konings schuiling 2008 - 2014
// this work is public domain. the code is undocumented, scruffy, untested, and should generally not be used for anything important.
// i do not offer support, so don't ask. to be used for inspiration :)

#include "aabb.hpp"

#include <climits>
#include <algorithm>
#include <cmath>

namespace blunted {

  AABB::AABB() {
  }

  AABB::AABB(const AABB &src) {
    this->minxyz = src.minxyz;
    this->maxxyz = src.maxxyz;
    radius_needupdate = true;
    center_needupdate = true;
  }

  AABB::~AABB() {
  }


  AABB AABB::operator += (const AABB &add) {
    if (minxyz.coords[0] > add.minxyz.coords[0]) minxyz.coords[0] = add.minxyz.coords[0];
    if (minxyz.coords[1] > add.minxyz.coords[1]) minxyz.coords[1] = add.minxyz.coords[1];
    if (minxyz.coords[2] > add.minxyz.coords[2]) minxyz.coords[2] = add.minxyz.coords[2];
    if (maxxyz.coords[0] < add.maxxyz.coords[0]) maxxyz.coords[0] = add.maxxyz.coords[0];
    if (maxxyz.coords[1] < add.maxxyz.coords[1]) maxxyz.coords[1] = add.maxxyz.coords[1];
    if (maxxyz.coords[2] < add.maxxyz.coords[2]) maxxyz.coords[2] = add.maxxyz.coords[2];
    radius_needupdate = true;
    center_needupdate = true;
    return *this;
  }

  AABB AABB::operator + (const Vector3 &vec) const {
    AABB aabb(*this);
    aabb.minxyz += vec;
    aabb.maxxyz += vec;
    radius_needupdate = true;
    center_needupdate = true;
    return aabb;
  }

  AABB AABB::operator * (const Quaternion &rot) const {
    AABB aabb;
    Vector3 vecs[8] = {
      Vector3(minxyz.coords[0], minxyz.coords[1], minxyz.coords[2]),
      Vector3(maxxyz.coords[0], minxyz.coords[1], minxyz.coords[2]),
      Vector3(minxyz.coords[0], maxxyz.coords[1], minxyz.coords[2]),
      Vector3(maxxyz.coords[0], maxxyz.coords[1], minxyz.coords[2]),
      Vector3(minxyz.coords[0], minxyz.coords[1], maxxyz.coords[2]),
      Vector3(maxxyz.coords[0], minxyz.coords[1], maxxyz.coords[2]),
      Vector3(minxyz.coords[0], maxxyz.coords[1], maxxyz.coords[2]),
      Vector3(maxxyz.coords[0], maxxyz.coords[1], maxxyz.coords[2])
    };

    Vector3 first = rot * vecs[0];
    aabb.minxyz = first;
    aabb.maxxyz = first;

    for (int i = 1; i < 8; i++) {
      Vector3 r = rot * vecs[i];
      if (r.coords[0] < aabb.minxyz.coords[0]) aabb.minxyz.coords[0] = r.coords[0];
      if (r.coords[0] > aabb.maxxyz.coords[0]) aabb.maxxyz.coords[0] = r.coords[0];
      if (r.coords[1] < aabb.minxyz.coords[1]) aabb.minxyz.coords[1] = r.coords[1];
      if (r.coords[1] > aabb.maxxyz.coords[1]) aabb.maxxyz.coords[1] = r.coords[1];
      if (r.coords[2] < aabb.minxyz.coords[2]) aabb.minxyz.coords[2] = r.coords[2];
      if (r.coords[2] > aabb.maxxyz.coords[2]) aabb.maxxyz.coords[2] = r.coords[2];
    }

    aabb.radius_needupdate = true;
    aabb.center_needupdate = true;
    radius_needupdate = true;
    center_needupdate = true;
    return aabb;
  }

  void AABB::Reset() {
    minxyz.Set( std::numeric_limits<real>::max(),  std::numeric_limits<real>::max(),  std::numeric_limits<real>::max());
    maxxyz.Set(-std::numeric_limits<real>::max(), -std::numeric_limits<real>::max(), -std::numeric_limits<real>::max());
    radius_needupdate = true;
    center_needupdate = true;
  }

  void AABB::SetMinXYZ(const Vector3 &min) {
    minxyz = min;
    MakeDirty();
  }

  void AABB::SetMaxXYZ(const Vector3 &max) {
    maxxyz = max;
    MakeDirty();
  }

  real AABB::GetRadius() const {
    if (radius_needupdate) {
      real x = maxxyz.coords[0] - minxyz.coords[0];
      real y = maxxyz.coords[1] - minxyz.coords[1];
      real z = maxxyz.coords[2] - minxyz.coords[2];
      radius = std::sqrt(x * x + y * y + z * z) * 0.5f;
      radius_needupdate = false;
    }
    return radius;
  }

  void AABB::GetCenter(Vector3 &center) const {
    if (center_needupdate) {
      real x = (minxyz.coords[0] + maxxyz.coords[0]) * 0.5f;
      real y = (minxyz.coords[1] + maxxyz.coords[1]) * 0.5f;
      real z = (minxyz.coords[2] + maxxyz.coords[2]) * 0.5f;
      this->center.Set(x, y, z);
      center_needupdate = false;
    }
    center.Set(this->center);
  }

  bool AABB::Intersects(const Vector3 &center, const real radius) const {
    real s, d = 0;

    for (int i = 0; i < 3; i++) {
      if (center.coords[i] < minxyz.coords[i]) {
        s = center.coords[i] - minxyz.coords[i];
        d += s * s;
      } else if (center.coords[i] > maxxyz.coords[i]) {
        s = center.coords[i] - maxxyz.coords[i];
        d += s * s;
      }
    }
    return d <= radius * radius;
  }

  bool AABB::Intersects(const vector_Planes &planes) const {
    const int planes_size = static_cast<int>(planes.size());
    for (int p = 0; p < planes_size; p++) {
      const Plane &pl = planes[p];
      const Vector3 &n = pl.GetVertex(1);
      // Positive vertex (p-vertex) maximizes dot(n, corner); <= 0 iff ALL 8 corners are <= 0!
      const real px = (n.coords[0] >= 0.0f) ? maxxyz.coords[0] : minxyz.coords[0];
      const real py = (n.coords[1] >= 0.0f) ? maxxyz.coords[1] : minxyz.coords[1];
      const real pz = (n.coords[2] >= 0.0f) ? maxxyz.coords[2] : minxyz.coords[2];
      const real maxDist = pl.GetDeterminant() + n.coords[0] * px + n.coords[1] * py + n.coords[2] * pz;
      if (maxDist <= 0.0f) {
        return false;
      }
    }
    return true;
  }

  bool AABB::Intersects(const AABB &src) const {
    if ((src.minxyz.coords[0] < maxxyz.coords[0] &&
         src.minxyz.coords[1] < maxxyz.coords[1] &&
         src.minxyz.coords[2] < maxxyz.coords[2]) &&
        (src.maxxyz.coords[0] > minxyz.coords[0] &&
         src.maxxyz.coords[1] > minxyz.coords[1] &&
         src.maxxyz.coords[2] > minxyz.coords[2])) {
      return true;
    } else {
      return false;
    }
  }

  bool AABB::Intersects(const Line &src) const {
    // http://www.gamedev.net/community/forums/topic.asp?topic_id=338987

    Vector3 p1 = src.GetVertex(0);
    Vector3 p2 = src.GetVertex(1);

    Vector3 d = (p2 - p1) * 0.5f;
    Vector3 e = (maxxyz - minxyz) * 0.5f;
    Vector3 c = p1 + d - (minxyz + maxxyz) * 0.5f;
    Vector3 ad = d.GetAbsolute(); // returns same vector with all components positive

    if (fabsf(c.coords[0]) > e.coords[0] + ad.coords[0]) return false;
    if (fabsf(c.coords[1]) > e.coords[1] + ad.coords[1]) return false;
    if (fabsf(c.coords[2]) > e.coords[2] + ad.coords[2]) return false;

    if (fabsf(d.coords[1] * c.coords[2] - d.coords[2] * c.coords[1]) > e.coords[1] * ad.coords[2] + e.coords[2] * ad.coords[1] + std::numeric_limits<float>::denorm_min()) return false;
    if (fabsf(d.coords[2] * c.coords[0] - d.coords[0] * c.coords[2]) > e.coords[2] * ad.coords[0] + e.coords[0] * ad.coords[2] + std::numeric_limits<float>::denorm_min()) return false;
    if (fabsf(d.coords[0] * c.coords[1] - d.coords[1] * c.coords[0]) > e.coords[0] * ad.coords[1] + e.coords[1] * ad.coords[0] + std::numeric_limits<float>::denorm_min()) return false;

    return true;
  }

}
