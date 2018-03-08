/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <stdio.h>
#include <math.h>

class router;

class MapEntry {
public:
  MapEntry() {}
  MapEntry(unsigned long long base, MapEntry *left, MapEntry *right);

  void insert(router *router);

  string target_name;
  MapEntry *next = NULL;
  unsigned long long base = 0;
  unsigned long long lowestBase = 0;
  unsigned long long size = 0;
  unsigned long long remove_offset = 0;
  unsigned long long add_offset = 0;
  uint32_t latency = 0;
  int64_t nextPacketTime = 0;
  MapEntry *left = NULL;
  MapEntry *right = NULL;
  vp::io_slave *port = NULL;
  vp::io_master *itf = NULL;
};

class io_master_map : public vp::io_master
{

  inline void bind_to(cm::port *port, cm::config *config);

};

class router : public vp::component
{

  friend class MapEntry;

public:

  router(const char *config);

  void build();
  void start();

  static vp::io_req_status_e req(void *__this, vp::io_req *req);


  static void grant(void *_this, vp::io_req *req);

  static void response(void *_this, vp::io_req *req);

private:
  vp::trace     trace;

  io_master_map out;
  vp::io_slave in;

  MapEntry *firstMapEntry = NULL;
  MapEntry *defaultMapEntry = NULL;
  MapEntry *errorMapEntry = NULL;
  MapEntry *topMapEntry = NULL;
  MapEntry *externalBindingMapEntry = NULL;

  int bandwidth = 0;
};

router::router(const char *config)
: vp::component(config)
{

}

MapEntry::MapEntry(unsigned long long base, MapEntry *left, MapEntry *right) : next(NULL), base(base), lowestBase(left->lowestBase), left(left), right(right) {
}

void MapEntry::insert(router *router)
{
  lowestBase = base;

  if (size != 0) {
    if (port != NULL || itf != NULL) {    
      MapEntry *current = router->firstMapEntry;
      MapEntry *prev = NULL;

      while(current && current->base < base) {
        prev = current;
        current = current->next;
      }
      
      if (prev == NULL) {
        next = router->firstMapEntry;
        router->firstMapEntry = this;
      } else {
        next = current;
        prev->next = this;
      }
    } else {
      router->errorMapEntry = this;
    }
  } else {
    router->defaultMapEntry = this;
  }
}

vp::io_req_status_e router::req(void *__this, vp::io_req *req)
{
  router *_this = (router *)__this;
  MapEntry *entry = _this->topMapEntry;
  uint64_t offset = req->get_addr();
  bool isRead = !req->get_is_write();
  uint64_t size = req->get_size();  

  _this->trace.msg("Received IO req (offset: 0x%llx, size: 0x%llx, isRead: %d)\n", offset, size, isRead);

  if (entry)
  {
    while(1) {
    // The entry does not have any child, this means we are at a final entry
      if (entry->left == NULL) break;

      if (offset >= entry->base) entry = entry->right;
      else entry = entry->left;
    }

    if (entry && (offset < entry->base || offset + size - 1 > entry->base + entry->size - 1)) {
      entry = NULL;
    }
  }

  if (!entry) {
    if (_this->errorMapEntry && offset >= _this->errorMapEntry->base && offset + size - 1 <= _this->errorMapEntry->base + _this->errorMapEntry->size - 1) {
    } else {
      entry = _this->defaultMapEntry;
    }
  }

  if (!entry) {
    //_this->trace.msg(&warning, "Invalid access (offset: 0x%llx, size: 0x%llx, isRead: %d)\n", offset, size, isRead);
    return vp::IO_REQ_INVALID;
  }

  if (entry == _this->defaultMapEntry) {
    _this->trace.msg("Routing to default entry (target: %s)\n", entry->target_name.c_str());
  } else {
    _this->trace.msg("Routing to entry (target: %s)\n", entry->target_name.c_str());
  }
  
#if 0
  if (_this->bandwidth != 0 and !req->is_debug()) {
    
  // Compute the duration from the specified bandwidth
  // Don't forget to compare to the already computed duration, as there might be a slower router
  // on the path
    req->set_duration((float)size / _this->bandwidth);

    // This is the time when the router is available
    int64_t routerTime = max(getCycles(), entry->nextPacketTime);

    // This is the time when the packet is available for the next module
    // It is either delayed by the router in case of bandwidth overflow, and in this case
    // we only apply the router latency, or it is delayed by the latency of the components 
    // on the path plus the router latency.
    // Just select the maximum
    int64_t packetTime = max(routerTime + entry->latency, getCycles() + req->getLatency() + entry->latency);

    // Compute the latency to be reported from the estimated packet time at the output
    req->setLatency(packetTime - getCycles());

    // Update the bandwidth information
    entry->nextPacketTime = routerTime + req->getLength();

  } else {
    req->incLatency(entry->latency);
  }
#endif

  // Forward the request to the target port
  if (entry->remove_offset) req->set_addr(offset - entry->remove_offset);
  if (entry->add_offset) req->set_addr(offset + entry->add_offset);
  vp::io_req_status_e result = vp::IO_REQ_OK;
  if (entry->port)
  {
    result = _this->out.req(req, entry->port);
  }
  else if (entry->itf)
  {
    result = entry->itf->req_forward(req);
  }

  return result;
}

void router::grant(void *_this, vp::io_req *req)
{
}

void router::response(void *_this, vp::io_req *req)
{
}

void router::build()
{
  traces.new_trace("trace", &trace, vp::DEBUG);

  in.set_req_meth(&router::req);
  new_slave_port("in", &in);

  out.set_resp_meth(&router::response);
  out.set_grant_meth(&router::grant);
  new_master_port("out", &out);

  bandwidth = get_config_int("bandwidth");

  cm::config *mappings = get_config()->get("mappings");

  if (mappings != NULL)
  {
    for (auto& mapping: mappings->get_childs())
    {
      cm::config *config = mapping.second;

      MapEntry *entry = new MapEntry();

      vp::io_master *itf = new vp::io_master();

      itf->set_resp_meth(&router::response);
      itf->set_grant_meth(&router::grant);
      new_master_port(mapping.first, itf);
      entry->itf = itf;

      cm::config *conf;
      conf = config->get("base");
      if (conf) entry->base = conf->get_int();
      conf = config->get("size");
      if (conf) entry->size = conf->get_int();
      conf = config->get("remove_offset");
      if (conf) entry->remove_offset = conf->get_int();
      conf = config->get("add_offset");
      if (conf) entry->add_offset = conf->get_int();
      entry->insert(this);
    }
  }
}

extern "C" void *vp_constructor(const char *config)
{
  return (void *)new router(config);
}




#define max(a, b) ((a) > (b) ? (a) : (b))

void router::start() {

  MapEntry *current = firstMapEntry;
  trace.msg("Building router table\n");
  while(current) {
    trace.msg("  0x%16llx : 0x%16llx -> %s\n", current->base, current->base + current->size, current->target_name.c_str());
    current = current->next;
  }
  if (errorMapEntry != NULL) {
    trace.msg("  0x%16llx : 0x%16llx -> ERROR\n", errorMapEntry->base, errorMapEntry->base + errorMapEntry->size);
  }
  if (defaultMapEntry != NULL) {
    trace.msg("       -     :      -     -> %s\n", defaultMapEntry->target_name.c_str());
  }

  MapEntry *firstInLevel = firstMapEntry;

  // Loop until we merged everything into a single entry
  // Start with the routing table entries
  current = firstMapEntry;

  // The first loop is here to loop until we have merged everything to a single entry
  while(1) {

    // Gives the map entry that should be on the left side of the next created entry
    MapEntry *left = NULL;

    // Gives the last allocated entry where the entry should be inserted
    MapEntry *currentInLevel=NULL;

    // The second loop is iterating on a single level to merge entries 2 by 2
    while(current) {
      if (left == NULL) left = current;
      else {

        MapEntry *entry = new MapEntry(current->lowestBase, left, current);

        left = NULL;
        if (currentInLevel) {
          currentInLevel->next = entry;
        } else {
          firstInLevel = entry;
        }
        currentInLevel = entry;
      }

      current = current->next;
    }

    current = firstInLevel;

    // Stop in case we got a single entry
    if (currentInLevel == NULL) break;

    // In case an entry is alone, insert it at the end, it will be merged with an upper entry
    if (left != NULL) {
      currentInLevel->next = left;
    }
  }

  topMapEntry = firstInLevel;
}

inline void io_master_map::bind_to(cm::port *_port, cm::config *config)
{
  MapEntry *entry = new MapEntry();
  cm::config *conf;
  entry->port = (vp::io_slave *)_port;
  if (config)
  {
    conf = config->get("base");
    if (conf) entry->base = conf->get_int();
    conf = config->get("size");
    if (conf) entry->size = conf->get_int();
    conf = config->get("remove_offset");
    if (conf) entry->remove_offset = conf->get_int();
    conf = config->get("add_offset");
    if (conf) entry->add_offset = conf->get_int();
  }
  entry->insert((router *)get_comp());
}