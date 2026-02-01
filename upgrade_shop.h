#pragma once

#include "libdragon.h"
#include <stdbool.h>

typedef enum
{
    UPGRADE_SHOP_RESULT_NONE,
    UPGRADE_SHOP_RESULT_OPEN,
    UPGRADE_SHOP_RESULT_EXIT
} eUpgradeShopResult;

void upgrade_shop_init(void);
void upgrade_shop_free(void);
eUpgradeShopResult upgrade_shop_update(bool _bCDown);
void upgrade_shop_render(void);
bool upgrade_shop_is_active(void);
