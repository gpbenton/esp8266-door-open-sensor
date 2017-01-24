# esp8266-door-open-sensor
Door open detector using esp8266 native code in platformio environment

I saw http://www.esp8266.com/viewtopic.php?f=11&t=4458 and as I had a spare esp01 module I thought it would be worth a try.

If the link stops working, here is the diagram 

<pre>
-----------------------------------
     |VCC  |RST                 |
   --------------                /  
   |             |              /   Switch, Normally Open
   |             | CH_PD        |
   |             |---------------
   |             |              |
   |             |GPIO0         |
   |             |---/\/\/\/\----
   |             |     1KOhm    |
   |             |              /
   |             |              \ 10KOhm
   |             |              /
   |             |              \
   |             |              |
   --------------               |
       | GND                    |
---------------------------------
</pre>

The idea is that the esp8266 is powered off by CH_PD dragged low until the switch is closed.

On power up, GPIO0 is taken high to keep CH_PD high until processing is finished.

## Software
I had wanted to try out Platformio development environment, and this seemed a perfect opportunity.

All my sensors use MQTT, so I can easily swap between different controlling software so I have included esp_mqtt from @tuanpmt as a submodule, and the platformio.init is configured to use this.  

The only change I have had to make to the esp_mqtt is to specify the include directory for queue.h in queue.c as this was conflicting with queue.h in the development environment, and I couldn't find a way of changing the Include path search order.
<pre>
#include "include/queue.h"
</pre>

