package ml.dmlc.xgboost4j.scala.app

import ml.dmlc.xgboost4j.java.app.{AbstractSparkApp, ParamUtils}
import ml.dmlc.xgboost4j.scala.spark.XGBoostClassifier
import org.apache.spark.ml.evaluation.BinaryClassificationEvaluator
import org.apache.spark.ml.util.FedMLUtils.FED_LIBSVM

import scala.collection.mutable

object XGBClassifierRunner extends AbstractSparkApp {

  val evaluator = new BinaryClassificationEvaluator()
    .setLabelCol("label")
    .setRawPredictionCol("rawPrediction")
    .setMetricName("areaUnderROC")

  def main(args: Array[String]): Unit = {
    setXgbParams(ParamUtils.params)
    val params = ParamUtils.fromArgs(args)

    val trainInput = spark.read.format(FED_LIBSVM).load(params("input").toString)
    val xgbClassifier = new XGBoostClassifier(params.toMap)
      .setNumRound(params("num_round").toString.toInt)
    // training
    val xgbModel = xgbClassifier.fit(trainInput)
    // output model
    xgbModel.write.overwrite().option("format", "json").save(params("model_output").toString)
  }

  def setXgbParams(params: mutable.HashMap[String, Any]) {
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
