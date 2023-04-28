#!/usr/bin/env bash

# shellcheck disable=SC2046
CD=$(cd $(dirname "$0") || exit; pwd)
# shellcheck disable=SC2034
WD=$(cd $(dirname "$CD") || exit; pwd)

base_path=$WD/xgboost4j/src/main/resources/lib/linux/x86_64
export LD_LIBRARY_PATH=$base_path/boost/lib:$base_path/grpc/lib:$base_path/grpc/lib64

input_path=../data/a9a.train
test_input_path=../data/a9a.test
model_output_path=./model/a9a.xgb.spark
fl_on=0 \
fl_comm_type=none
num_round=3
local=true
is_spark=false

show_usage="args: [-h|--help  -i|--input_path  -s|--is_spark]  \n\
-h|--help              \t   show help information  \n\
-i|--input_path        \t   input path \n\
-t|--test_input_path   \t   test input path \n\
-m|--model_output_path \t   model output path \n\
-o|--fl_on             \t   whether federated is on \n\
-c|--fl_comm_type      \t   federated communication type \n\
-n|--num_round         \t   num_round \n\
-l|--local             \t   whether is local \n\
-s|--is_spark          \t   whether app is based on spark"
ARGS=$(getopt -o hi:t:m:oc:n:ls -l help,input_path:,test_input_path:,model_output_path:,fl_on,fl_comm_type:,num_round:,local,is_spark -n 'xgb-run.sh' -- "$@")
# shellcheck disable=SC2181
if [[ $? != 0 ]]; then
  echo "Terminating..."
  exit 1
fi
eval set -- "${ARGS}"

while true
do
  case $1 in
    -h|--help) echo -e "$show_usage"; exit 0;;
    -i|--input_path) input_path=$2; shift 2;;
    -t|--test_input_path) test_input_path=$2; shift 2;;
    -m|--model_output_path) model_output_path=$2; shift 2;;
    -o|--fl_on) shift; fl_on=1;;
    -c|--fl_comm_type) fl_comm_type=$2; shift 2;;
    -n|--num_round) num_round=$2; shift 2;;
    -l|--local) shift; local=true;;
    -s|--is_spark) shift; is_spark=true;;
    --) shift; break;;
    *) echo "unknown args"; exit 1;;
  esac
done

scala_v=2.12
spark_v=3.0.1
java -classpath xgboost4j-federated-app/target/xgboost4j-federated-app_$scala_v-2.0.0-SNAPSHOT-jar-with-dependencies.jar:\
"$HOME"/.m2/repository/org/apache/spark/spark-core_$scala_v/$spark_v/spark-core_$scala_v-$spark_v.jar:\
"$HOME"/.m2/repository/org/apache/spark/spark-sql_$scala_v/$spark_v/spark-sql_$scala_v-$spark_v.jar:\
"$HOME"/.m2/repository/org/apache/spark/spark-mllib_$scala_v/$spark_v/spark-mllib_$scala_v-$spark_v.jar:\
"$HOME"/.m2/repository/org/apache/spark/spark-catalyst_$scala_v/$spark_v/spark-catalyst_$scala_v-$spark_v.jar:\
"$HOME"/.m2/repository/org/apache/spark/spark-network-common_$scala_v/$spark_v/spark-network-common_$scala_v-$spark_v.jar:\
"$HOME"/.m2/repository/org/apache/spark/spark-unsafe_$scala_v/$spark_v/spark-unsafe_$scala_v-$spark_v.jar:\
"$HOME"/.m2/repository/org/json4s/json4s-core_$scala_v/3.6.6/json4s-core_$scala_v-3.6.6.jar:\
"$HOME"/.m2/repository/org/json4s/json4s-ast_$scala_v/3.6.6/json4s-ast_$scala_v-3.6.6.jar \
ml.dmlc.xgboost4j.scala.fl.XGBClassifierRunner \
input_path="$input_path" test_input_path="$test_input_path" model_output_path="$model_output_path" fl_on=$fl_on \
fl_comm_type="$fl_comm_type" num_round="$num_round" local=$local is_spark=$is_spark
