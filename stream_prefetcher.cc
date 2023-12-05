#include <algorithm>
#include <array>
#include <map>

#include "cache.h"

constexpr int PREFETCH_DEGREE = 4;
constexpr int PREFETCH_DISTANCE = 2;
constexpr int FIXED_STREAM_SIZE = 64;


struct tracker_entry {
  uint64_t addr = 0;              // the IP we're tracking
  uint64_t last_cl_addr = 0;    // the last address accessed by this IP
  int64_t last_stride = 0;      // the stride between the last two addresses accessed by this IP
  uint64_t last_used_cycle = 0; // use LRU to evict old IP trackers
};

struct lookahead_entry {
  uint64_t address = 0;
  int64_t stride = 0;
  int degree = 0; // degree remaining
  int distance = 0; //distance
};
/*
struct lookahead_entry_data {
  uint64_t address = 0;
  int64_t last_stride = 0;
  
 };

*/

constexpr std::size_t TRACKER_SETS = 256;
constexpr std::size_t TRACKER_WAYS = 4;
//constexpr std::size_t CACHE_SETS = 1024;
//constexpr std::size_t CACHE_WAYS = 8;
std::map<CACHE*, lookahead_entry> lookahead;
std::map<CACHE*, std::array<tracker_entry, TRACKER_SETS * TRACKER_WAYS>> trackers;
//std::map<CACHE*, std::array<lookahead_entry_data,CACHE_SETS * CACHE_WAYS>> lookahead_data;
int stream_size =0;

void CACHE::prefetcher_initialize() { std::cout << NAME << " Stream prefetcher" << std::endl; }

void CACHE::prefetcher_cycle_operate()
{
   uint64_t pf_address;// If a lookahead is active
   int value=3;
   
  if (auto [old_pf_address, stride, degree, distance] = lookahead[this]; degree > 0) {
    //auto old_pfd_address = old_pf_address*64;
    //std::cout << "Im here in prefetch value" << old_pf_address << std::endl;
    //value = value-1;
    //std::cout << "Im here in prefetch " << pf_address << std::endl;
    //std::cout << "Im here in prefetch  " << degree << std::endl;
    if(degree == PREFETCH_DEGREE)
    {
     if(distance > 0){
        
        pf_address = old_pf_address  +  (PREFETCH_DISTANCE<<LOG2_BLOCK_SIZE)  +  (stride << LOG2_BLOCK_SIZE);
        }
     else
        pf_address = old_pf_address - (PREFETCH_DISTANCE<<LOG2_BLOCK_SIZE) - (stride << LOG2_BLOCK_SIZE);
    }
    else
    { 
      if(distance > 0)
        pf_address = old_pf_address + (stride << LOG2_BLOCK_SIZE);
     else
        pf_address = old_pf_address - (stride << LOG2_BLOCK_SIZE);
    }
    //std::cout << "Im here in prefetch " << pf_address << std::endl;
    //std::cout << "stride " << stride << std::endl;
    //std::cout << "pf_address  " << pf_address << std::endl;

    // If the next step would exceed the degree or run off the page, stop
    if (virtual_prefetch || (pf_address >> LOG2_PAGE_SIZE) == (old_pf_address >> LOG2_PAGE_SIZE)) {
      // check the MSHR occupancy to decide if we're going to prefetch to this
      // level or not
      bool success = prefetch_line(0, 0, pf_address, (get_occupancy(0, pf_address) < get_size(0, pf_address) / 2), 0);
      if (success)
        lookahead[this] = {pf_address, stride, degree-1,distance};
      // If we fail, try again next cycle
    } else {
      lookahead[this] = {};
    }
    
  }
  
}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in)
{
  uint64_t cl_addr = addr >> LOG2_BLOCK_SIZE;
  int64_t stride = 0;
  int direction=0;
  //std::cout << "Im here in cache operate prefetch  " << addr << std::endl;
  int distance=PREFETCH_DISTANCE;
  // get boundaries of tracking set
  auto set_begin = std::next(std::begin(trackers[this]), ip % TRACKER_SETS);
  auto set_end = std::next(set_begin, TRACKER_WAYS);
  //auto set_begin1 = std::next(std::begin(lookahead_data[this]), addr % CACHE_SETS);
  //auto set_end1 = std::next(set_begin, CACHE_WAYS);
  //std::cout << "ip  " << ip << std::endl;
  //std::cout << "address  " << addr << std::endl;
  //std::cout<<"cache_hit " << addr << std::endl; 
  // find the current ip within the set
  auto found = std::find_if(set_begin, set_end, [addr](tracker_entry x) { return x.addr == addr; });
  // auto found1 = std::find_if(set_begin1, set_end1, [addr](lookahead_entry_data y) { return y.address == addr; });
  // if()
  // std::cout << "found  " << found << std::endl;
  
  if (found == set_end) {
    // calculate the stride between the current address and the last address
    // no need to check for overflow since these values are downshifted
    
    stride = (int64_t)cl_addr - (int64_t)found->last_cl_addr;
    if(stride>0)
    {
      direction +=1;
      stream_size +=1;
      //lookahead[this] = {addr, stride, distance,distance};
      //std::cout << "distance  " << direction << std::endl;
    } 
    else
    {
      direction -=1;
      stream_size +=1;
    }
    if(stream_size == FIXED_STREAM_SIZE){
    
    if(direction >= 0)
    {
       distance = PREFETCH_DISTANCE;
      direction=0;
      
      //lookahead[this] = {addr, stride, PREFETCH_DEGREE,distance};
       //std::cout << "degree " << stream_size  << std::endl;
    }
    else if(direction < 0)
    {
       distance = -PREFETCH_DISTANCE;
      direction=0;
      
    }
    stream_size = 0;
    }

    

    // Initialize prefetch state unless we somehow saw the same address twice in
    // a row or if this is the first time we've seen this stride
    
      
  } else {
    // replace by LRU
    found = std::min_element(set_begin, set_end, [](tracker_entry x, tracker_entry y) { return x.last_used_cycle < y.last_used_cycle; });
  }
  lookahead[this] = {addr, stride, PREFETCH_DEGREE,direction};

  // update tracking set
  *found = {ip, cl_addr, stride, current_cycle};

  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void CACHE::prefetcher_final_stats() {}
