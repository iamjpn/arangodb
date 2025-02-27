////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2020 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Andrey Abramov
////////////////////////////////////////////////////////////////////////////////

#ifndef ARANGODB_IRESEARCH__IRESEARCH_GEO_ANALYZER
#define ARANGODB_IRESEARCH__IRESEARCH_GEO_ANALYZER 1

#include <s2/s2region_term_indexer.h>

#include "shared.hpp"
#include "analysis/token_attributes.hpp"
#include "analysis/analyzer.hpp"
#include "utils/frozen_attributes.hpp"

#include "Geo/ShapeContainer.h"
#include "IResearch/Geo.h"
#include "velocypack/Slice.h"
#include "velocypack/velocypack-aliases.h"

namespace arangodb {

namespace velocypack {
template<typename T> class Buffer;
}

namespace iresearch {

class GeoAnalyzer
  : public irs::frozen_attributes<2, irs::analysis::analyzer>,
    private irs::util::noncopyable {
 public:
  virtual bool next() noexcept final;
  using irs::analysis::analyzer::reset;

  virtual void prepare(S2RegionTermIndexer::Options& opts) const = 0;

 protected:
  explicit GeoAnalyzer(const irs::type_info& type);
  void reset(std::vector<std::string>&& terms) noexcept;
  void reset() noexcept {
    _begin = _terms.data();
    _end = _begin;
  }

 private:
  std::vector<std::string> _terms;
  const std::string* _begin{ _terms.data() };
  const std::string* _end{ _begin };
  irs::offset _offset;
  irs::increment _inc;
  irs::term_attribute _term;
}; // GeoAnalyzer

class GeoPointAnalyzer final : public GeoAnalyzer {
 public:
  struct Options {
    GeoOptions options;
    std::string latitude;
    std::string longitude;
  };

  static constexpr irs::string_ref type_name() noexcept { return "geopoint"; }
  static bool normalize(const irs::string_ref& args,  std::string& out);
  static irs::analysis::analyzer::ptr make(irs::string_ref const& args);

  // store point as [lng, lat] array to be GeoJSON compliant
  static VPackSlice store(
    irs::token_stream const* ctx,
    VPackSlice slice,
    velocypack::Buffer<uint8_t>& buf);

  explicit GeoPointAnalyzer(Options const& opts);

  virtual void prepare(S2RegionTermIndexer::Options& opts) const;

 protected:
  virtual bool reset(irs::string_ref const& value);

 private:
  bool parsePoint(VPackSlice slice, S2LatLng& out) const;

  S2RegionTermIndexer _indexer;
  std::string _latitude;
  std::string _longitude;
  S2LatLng _point;
  bool _fromArray;
};

class GeoJSONAnalyzer final : public GeoAnalyzer {
 public:
  enum class Type : uint32_t {
    SHAPE = 0,
    CENTROID,
    POINT
  };

  struct Options {
    GeoOptions options;
    Type type{Type::SHAPE};
  };

  static constexpr irs::string_ref type_name() noexcept { return "geojson"; }
  static bool normalize(const irs::string_ref& args,  std::string& out);
  static irs::analysis::analyzer::ptr make(irs::string_ref const& args);

  static VPackSlice store(
      irs::token_stream const*,
      VPackSlice slice,
      velocypack::Buffer<uint8_t>&) noexcept {
    return slice;
  }

  explicit GeoJSONAnalyzer(Options const& opts);

  virtual void prepare(S2RegionTermIndexer::Options& opts) const;

 protected:
  virtual bool reset(irs::string_ref const& value);

 private:
  S2RegionTermIndexer _indexer;
  geo::ShapeContainer _shape;
  Type _type;
};

// FIXME remove kludge
inline bool isGeoAnalyzer(irs::string_ref const& type) noexcept {
  return type == GeoJSONAnalyzer::type_name()
    || type == GeoPointAnalyzer::type_name();
}

} // iresearch
} // arangodb

#endif // ARANGODB_IRESEARCH__IRESEARCH_GEO_ANALYZER
