////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2020 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// @author Andreas Streichardt
////////////////////////////////////////////////////////////////////////////////

#include "AgencyCallbackRegistry.h"

#include <ctime>

#include <velocypack/Slice.h>
#include <velocypack/velocypack-aliases.h>

#include "ApplicationFeatures/ApplicationServer.h"
#include "Basics/Exceptions.h"
#include "Basics/ReadLocker.h"
#include "Basics/WriteLocker.h"
#include "Cluster/AgencyCache.h"
#include "Cluster/ServerState.h"
#include "Endpoint/Endpoint.h"
#include "Logger/LogMacros.h"
#include "Logger/Logger.h"
#include "Logger/LoggerStream.h"
#include "Random/RandomGenerator.h"

using namespace arangodb;

AgencyCallbackRegistry::AgencyCallbackRegistry(application_features::ApplicationServer& server,
                                               std::string const& callbackBasePath)
  : _agency(server), _callbackBasePath(callbackBasePath) {}

AgencyCallbackRegistry::~AgencyCallbackRegistry() = default;

bool AgencyCallbackRegistry::registerCallback(std::shared_ptr<AgencyCallback> cb, bool local) {
  uint64_t rand;
  {
    WRITE_LOCKER(locker, _lock);
    while (true) {
      rand = RandomGenerator::interval(std::numeric_limits<uint64_t>::max());
      if (_endpoints.try_emplace(rand, cb).second) {
        break;
      }
    }
  }

  
  bool ok = false;
  try {
    if (local) {
      auto& cache = _agency.server().getFeature<ClusterFeature>().agencyCache();
      ok = cache.registerCallback(cb->key, rand);
    } else {
      ok = _agency.registerCallback(cb->key, getEndpointUrl(rand)).successful();
      cb->local(false);
    }
    if (!ok) {
      LOG_TOPIC("b88f4", ERR, Logger::CLUSTER) << "Registering callback failed";
    }
  } catch (std::exception const& e) {
    LOG_TOPIC("f5330", ERR, Logger::CLUSTER) << "Couldn't register callback " << e.what();
  } catch (...) {
    LOG_TOPIC("1d24f", ERR, Logger::CLUSTER)
        << "Couldn't register callback. Unknown exception";
  }
  if (!ok) {
    WRITE_LOCKER(locker, _lock);
    _endpoints.erase(rand);
  }
  return ok;
}

std::shared_ptr<AgencyCallback> AgencyCallbackRegistry::getCallback(uint64_t id) {
  READ_LOCKER(locker, _lock);
  auto it = _endpoints.find(id);

  if (it == _endpoints.end()) {
    return nullptr;
  }
  return (*it).second;
}

bool AgencyCallbackRegistry::unregisterCallback(std::shared_ptr<AgencyCallback> cb) {
  bool found = false;
  uint64_t endpointToDelete = 0;
  {
    // find the key of the callback while only holding a read lock
    READ_LOCKER(locker, _lock);

    for (auto const& it : _endpoints) {
      if (it.second.get() == cb.get()) {
        endpointToDelete = it.first;
        found = true;
        break;
      }
    }
  }

  // if we found the callback, we need to unregister it and remove it from
  // the map. that requires a write lock
  if (found) {
    {
      WRITE_LOCKER(locker, _lock);
      if (_endpoints.erase(endpointToDelete) == 0) {
        // callback not in map anymore. this can only happen if this method
        // is called concurrently for the same callback and one thread has
        // already deleted it from the map. in this case we act like as if
        // the callback was not found, and leave the callback handling to
        // the other thread.
        found = false;
      }
    }
    // we need to release the write lock for the map already, because we are
    // now calling into other methods which may also acquire locks. and we
    // don't want to be vulnerable to priority inversion.
    
    if (found) {
      if (cb->local()) {
        auto& cache = _agency.server().getFeature<ClusterFeature>().agencyCache();
        cache.unregisterCallback(cb->key, endpointToDelete);
      } else {
        _agency.unregisterCallback(cb->key, getEndpointUrl(endpointToDelete));
      }
    }
  }
  return found;
}

std::string AgencyCallbackRegistry::getEndpointUrl(uint64_t endpoint) const {
  std::stringstream url;
  url << Endpoint::uriForm(ServerState::instance()->getEndpoint())
      << _callbackBasePath << "/" << endpoint;

  return url.str();
}
