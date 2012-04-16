//Copyright (C)2011 Armin.Weatherwax at googlemail dot com
//License: gpl v2 +floss exception as in Imprudence or at your choice lgpl v2.1 as in Kokua 
#ifndef IMP_ENVIRONMENTSETTINGS_H
#define IMP_ENVIRONMENTSETTINGS_H

#include "lluuid.h"
#include "llmemory.h"  // LLSingleton<>

class IMPEnvironmentSettings : public LLSingleton<IMPEnvironmentSettings>
{
public:
  IMPEnvironmentSettings();
  void init();
  void getEnvironmentSettings();
private:
  static void idle(void *user_data);
  LLUUID mRegionID;
};

#endif //IMP_ENVIRONMENTSETTINGS_H