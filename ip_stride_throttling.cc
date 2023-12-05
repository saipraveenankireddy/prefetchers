#include <algorithm>
#include <array>
#include <map>

#include "cache.h"

constexpr int PREFETCH_DEGREE = 4;
constexpr int PREFETCH_DISTANCE = 1;
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
  //int distance = 0;
  int distance_prefetch = 0; //distance
  int modified_degree = 0;
};

constexpr std::size_t TRACKER_SETS = 256;
constexpr std::size_t TRACKER_WAYS = 4;
//constexpr std::size_t CACHE_SETS = 1024;
//constexpr std::size_t CACHE_WAYS = 8;
std::map<CACHE*, lookahead_entry> lookahead;
std::map<CACHE*, std::array<tracker_entry, TRACKER_SETS * TRACKER_WAYS>> trackers;
uint64_t track_array[16];
//std::map<CACHE*, std::array<lookahead_entry_data,CACHE_SETS * CACHE_WAYS>> lookahead_data;

void CACHE::prefetcher_initialize() { std::cout << NAME << "ip stride Throttling prefetcher" << std::endl; }

void CACHE::prefetcher_cycle_operate()
{
   uint64_t pf_address;// If a lookahead is active
   int count=0;
   //std::cout << "im herein prefetcher "  << std::endl;
   //int check=0;
  if (auto [old_pf_address, stride, degree,distance_prefetch,modified_degree] = lookahead[this]; degree > 0) {
    //auto old_pfd_address = old_pf_address*64;
    //check=1;
    //std::cout << "im herein prefetcher " << pf_address  << std::endl;
    if(degree == modified_degree)
    {
        
        pf_address = old_pf_address +  (distance_prefetch<<LOG2_BLOCK_SIZE)+(stride << LOG2_BLOCK_SIZE);
        
     }
    else
    { 
        pf_address = old_pf_address + (stride << LOG2_BLOCK_SIZE);
        
    }
      //std::cout << "im herein prefetcher "<< pf_address  << std::endl;
    
   if(count >= 0)
    {
       track_array[count] = pf_address;
       count += 1;
    }
            //std::cout << "stride " << stride << std::endl;
    //std::cout << "pf_address  " << pf_address << std::endl;
    // If the next step would exceed the degree or run off the page, stop
    if (virtual_prefetch || (pf_address >> LOG2_PAGE_SIZE) == (old_pf_address >> LOG2_PAGE_SIZE)) {
      // check the MSHR occupancy to decide if we're going to prefetch to this
      // level or not
      bool success = prefetch_line(0, 0, pf_address, (get_occupancy(0, pf_address) < get_size(0, pf_address) / 2), 0);
      if (success)
        lookahead[this] = {pf_address, stride, degree - 1,distance_prefetch,modified_degree};
      // If we fail, try again next cycle
    } else {
      lookahead[this] = {};
    }
  }
  count = 0;
  }
    

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, uint8_t type, uint32_t metadata_in)
{
  uint64_t cl_addr = addr >> LOG2_BLOCK_SIZE;
  int64_t stride = 0;
  int direction=0;
  int stream_size =0;
  int distance=0;
  int distance_prefetch = PREFETCH_DISTANCE; 
  int hit=0;
  int counter=0;
  int accuracy=0;
  int modified_degree = PREFETCH_DEGREE;
  // get boundaries of tracking set
  auto set_begin = std::next(std::begin(trackers[this]), ip % TRACKER_SETS);
  auto set_end = std::next(set_begin, TRACKER_WAYS);
  //auto set_begin1 = std::next(std::begin(lookahead_data[this]), addr % CACHE_SETS);
  //auto set_end1 = std::next(set_begin, CACHE_WAYS);
  //auto track_begin = track_array.begin();
  //auto track_end   = track_array.end();

  //std::cout << "ip  " << ip << std::endl;
  //std::cout << "address  " << addr << std::endl;
  //std::cout<<"cache_hit " << addr << std::endl; 
  // find the current ip within the set
  
  bool acc = false;
for (int i = 0; i < sizeof(track_array) / sizeof(track_array[0]); i++) {
    if (track_array[i] == addr) {
        acc = true;
        break;
    }
}

  if(acc){
     hit +=1;   
  }
  counter +=1;
  if((counter==3) && acc){
         accuracy = hit/modified_degree;
  }
  if(accuracy > 0.25)
  {
    modified_degree = modified_degree + 1;
    distance_prefetch = distance_prefetch + 1;
  }
  auto found = std::find_if(set_begin, set_end, [addr](tracker_entry x) { return x.addr == addr; });
  //auto found1 = std::find_if(set_begin1, set_end1, [addr](lookahead_entry_data y) { return y.address == addr; });
   //if()
   //std::cout << "im herein prefetcher" <<modified_degree<< std::endl;
   //std::cout << "im herein prefetcher" <<modified_degree<< std::endl;
 //std::cout << "im herein prefetcher operate" << addr << std::endl;
  // if we found a matching entry
  if (found == set_end) {
    // calculate the stride between the current address and the last address
    // no need to check for overflow since these values are downshifted
    
        
        lookahead[this] = {addr, stride, modified_degree,distance_prefetch,modified_degree};
    

    

    // Initialize prefetch state unless we somehow saw the same address twice in
    // a row or if this is the first time we've seen this stride
    
      
  } else {
    // replace by LRU
    found = std::min_element(set_begin, set_end, [](tracker_entry x, tracker_entry y) { return x.last_used_cycle < y.last_used_cycle; });
  }

  // update tracking set
  *found = {ip, cl_addr, stride, current_cycle};
     return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void CACHE::prefetcher_final_stats() {}
