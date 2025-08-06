#include "model.h"

std::shared_ptr<model_info> models::unknown = std::make_shared<model_unknown>();
std::shared_ptr<model_info> models::largest = std::make_shared<model_rp_generic>();
