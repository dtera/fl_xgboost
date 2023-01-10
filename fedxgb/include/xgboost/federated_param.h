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
  // data split mode, can be row, col, or none.
  DataSplitMode dsplit{DataSplitMode::kAuto};
  // federated role, can be guest or host
  FedratedRole fl_role{FedratedRole::Guest};
  std::string fl_address;  // for data holder part
  uint32_t fl_port;        // for label part
  uint32_t fl_bit_len;
  uint32_t fl_part_id;

  DMLC_DECLARE_PARAMETER(FederatedParam) {
    DMLC_DECLARE_FIELD(dsplit)
        .set_default(DataSplitMode::kAuto)
        .add_enum("auto", DataSplitMode::kAuto)
        .add_enum("col", DataSplitMode::kCol)
        .add_enum("row", DataSplitMode::kRow)
        .add_enum("none", DataSplitMode::kNone)
        .describe("Data split mode for distributed training.");
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
    DMLC_DECLARE_FIELD(fl_part_id)
        .set_default(0)
        .set_lower_bound(0)
        .describe("Part id for federated learning.");
  }
};
}  // namespace xgboost
