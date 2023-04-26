/*
 Copyright (c) 2023 by Contributors

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */
package ml.dmlc.xgboost4j.scala.fl

import ml.dmlc.xgboost4j.scala.spark.XGBoostClassifier
import org.apache.spark.ml.evaluation.BinaryClassificationEvaluator
import org.apache.spark.ml.util.FedMLUtils.FED_LIBSVM
import org.apache.spark.sql.DataFrame

import scala.collection.mutable

object XGBClassifierRunner extends AbstractSparkApp {

  val evaluator: BinaryClassificationEvaluator = new BinaryClassificationEvaluator()
    .setLabelCol("label")
    .setRawPredictionCol("rawPrediction")
    .setMetricName("areaUnderROC")

  private var local = false

  override def isLocal: Boolean = local

  def main(args: Array[String]): Unit = {
    setXgbParams(ParamUtils.params)
    val params = ParamUtils.fromArgs(args)
    local = if (params.contains("local")) params("local").toString.toBoolean else false

    val inputDF = spark.read.format(FED_LIBSVM).load(params("input_path").toString)
    val xgbClassifier = new XGBoostClassifier(params.toMap)
      .setNumRound(params("num_round").toString.toInt)

    train(inputDF, params, xgbClassifier)
  }

  def train(inputDF: DataFrame, params: mutable.HashMap[String, Any],
            xgbClassifier: XGBoostClassifier): Unit = {
    if (params.contains("test_input_path") && params("test_input_path").toString.nonEmpty) {
      val testInputDF = spark.read.format(FED_LIBSVM).load(params("test_input_path").toString)
      params += "eval_sets" -> Map("test" -> testInputDF)
    }
    // training
    val xgbModel = xgbClassifier.fit(inputDF)
    // output model
    val model_output_path = if (params.contains("model_output_path")) params("model_output_path").toString else ""
    xgbModel.write.overwrite().option("format", "json").save(model_output_path)

    /*
    if (params.contains("test_input_path") && params("test_input_path").toString.nonEmpty) {
      val testInputDF = spark.read.format(FED_LIBSVM).load(params("test_input_path").toString)
      val testAUC = evaluator.evaluate(xgbModel.transform(testInputDF))
      println(s"Test AUC: $testAUC")
    }
    */
  }

  def setXgbParams(params: mutable.HashMap[String, Any]): Unit = {
    params += "fl_on" -> 1
    params += "fl_bit_len" -> 1024
    params += "fl_comm_type" -> "pulsar"
    params += "fl_pulsar_url" -> "pulsar://localhost:6650"
    params += "fl_pulsar_topic_ttl" -> 5
    params += "fl_pulsar_topic_prefix" -> "federated_spark_xgb_"

    params += "booster" -> "gbtree"
    params += "eta" -> 1.0
    params += "gamma" -> 1.0
    params += "max_depth" -> 8
    params += "seed" -> 0
    params += "min_child_weight" -> 0
    params += "missing" -> 0f
    params += "verbosity" -> 1
    params += "objective" -> "reg:squarederror"
    params += "tree_method" -> "hist"
    params += "eval_metric" -> "auc"
    params += "dump_format" -> "json"
  }

}
