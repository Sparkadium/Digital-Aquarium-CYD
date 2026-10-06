#pragma once
#include <stdint.h>
// Debounce the resistive controller's PENIRQ; one pinch per contact.
// Feeding is at the surface, so no screen calibration or coordinate mapping is needed.
class TouchFeedGate {
public:
  void begin(bool pressed,uint32_t now){raw=pressed;fired=pressed;changedAt=now;}
  bool update(bool pressed,uint32_t now){
    if(pressed!=raw){raw=pressed;changedAt=now;}
    if((uint32_t)(now-changedAt)<40)return false;
    if(!raw){fired=false;return false;}
    if(fired)return false;fired=true;return true;
  }
private:
  bool raw=false,fired=false;uint32_t changedAt=0;
};
