//
// Created by HqZhao on 2022/12/20.
//

#pragma once

#include <xgboost/parameter.h>

namespace xgboost {
enum class FedratedRole : int { Guest = 0, Host = 1 };
enum class FedratedCommType : int { Socket = 0, Pulsar = 1 };
}  // namespace xgboost

DECLARE_FIELD_ENUM_CLASS(xgboost::FedratedRole);
DECLARE_FIELD_ENUM_CLASS(xgboost::FedratedCommType);

namespace xgboost {

struct FederatedParam : public XGBoostParameter<FederatedParam> {
  inline static std::string getRole(const FedratedRole &role) {
    switch (role) {
      case FedratedRole::Guest:
        return "guest";
      case FedratedRole::Host:
        return "host";
      default:
        return "";
    }
  }
  inline static std::string getCommType(const FedratedCommType &t) {
    switch (t) {
      case FedratedCommType::Socket:
        return "socket";
      case FedratedCommType::Pulsar:
        return "pulsar";
      default:
        return "";
    }
  }
  // data split mode, can be row, col, or none.
  DataSplitMode dsplit{DataSplitMode::kAuto};
  // federated role, can be guest or host
  FedratedRole fl_role{FedratedRole::Guest};
  // federated role, can be guest or host
  FedratedCommType fl_comm_type{FedratedCommType::Socket};
  std::string fl_pulsar_url;
  std::string fl_pulsar_topic_prefix;
  std::string fl_pulsar_token;
  std::string fl_pulsar_tenant;
  std::string fl_pulsar_namespace;
  std::string fl_address;  // for data holder part
  uint32_t fl_port;        // for label part
  uint32_t fl_bit_len;
  uint32_t fl_part_id;
  uint32_t fl_on;

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
    DMLC_DECLARE_FIELD(fl_comm_type)
        .set_default(FedratedCommType::Socket)
        .add_enum("socket", FedratedCommType::Socket)
        .add_enum("pulsar", FedratedCommType::Pulsar)
        .describe("Communication type for Fedrated learning.");
    DMLC_DECLARE_FIELD(fl_pulsar_url)
        .set_default("pulsar://localhost:6650")
        .describe("Url for pulsar communication.");
    DMLC_DECLARE_FIELD(fl_pulsar_topic_prefix)
        .set_default("federated_xgb_")
        .describe("Topic prefix for pulsar communication.");
    DMLC_DECLARE_FIELD(fl_pulsar_token)
        .set_default("notoken")
        .describe("Token for pulsar communication.");
    DMLC_DECLARE_FIELD(fl_pulsar_tenant)
        .set_default("fl-tenant")
        .describe("Tenant for pulsar communication.");
    DMLC_DECLARE_FIELD(fl_pulsar_namespace)
        .set_default("fl-algorithm")
        .describe("Namespace for pulsar communication.");
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
    DMLC_DECLARE_FIELD(fl_on).set_default(0).set_lower_bound(0).describe(
        "Whether run with federated learning.");
  }
};
}  // namespace xgboost
