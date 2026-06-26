/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022-2023 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "settings.h"

int  setting_sampleSetting;
bool setting_sampleSetting2;

void settings_reset() {
    setting_sampleSetting  = 1;
    setting_sampleSetting2 = true;
}

void settings_load() {
    settings_reset();
}

void settings_save() {
}
