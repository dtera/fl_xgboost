//
// Created by HqZhao on 2022/12/20.
//

#pragma once

#include <xgboost/parameter.h>

namespace xgboost {
enum class FedratedRole : int { Guest = 0, Host = 1 };
}

DECLARE_FIELD_ENUM_CLASS(xgboost::FedratedRole);

namespace xgboost {

struct FederatedParam : public XGBoostParameter<FederatedParam> {
  // federated role, can be guest or host
  FedratedRole fl_role{FedratedRole::Guest};
  std::string fl_address;  // for host
  uint32_t fl_port;        // for guest
  uint32_t fl_bit_len;

  DMLC_DECLARE_PARAMETER(FederatedParam) {
    DMLC_DECLARE_FIELD(fl_role)
        .set_default(FedratedRole::Guest)
        .add_enum("guest", FedratedRole::Guest)
        .add_enum("host", FedratedRole::Host)
        .describe("Role for Fedrated learning.");
    DMLC_DECLARE_FIELD(fl_address)
        .set_default("0.0.0.0:50001")
        .describe("Address for grpc communication.");
    DMLC_DECLARE_FIELD(fl_port).set_default(50001).describe("Port for grpc communication.");
    DMLC_DECLARE_FIELD(fl_bit_len)
        .set_default(1024)
        .set_range(1024, 7680)
        .describe("Bit length of secret key.");
  }
};
}  // namespace xgboost
