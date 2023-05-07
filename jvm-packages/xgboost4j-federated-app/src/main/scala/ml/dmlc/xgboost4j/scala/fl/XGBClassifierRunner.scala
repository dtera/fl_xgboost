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

import ml.dmlc.xgboost4j.java.NativeLibLoader
import ml.dmlc.xgboost4j.scala.spark.XGBoostClassifier
import ml.dmlc.xgboost4j.scala.{DMatrix, XGBoost}
import org.apache.spark.SparkConf
import org.apache.spark.ml.evaluation.BinaryClassificationEvaluator
import org.apache.spark.ml.util.FedMLUtils.FED_LIBSVM
import org.apache.spark.sql.DataFrame

import java.io.File
import scala.collection.mutable

object XGBClassifierRunner extends AbstractSparkApp {

  val evaluator: BinaryClassificationEvaluator = new BinaryClassificationEvaluator()
    .setLabelCol("label")
    .setRawPredictionCol("rawPrediction")
    .setMetricName("areaUnderROC")

  private var local = false

  override def isLocal: Boolean = local

  override def initSparkConf(): SparkConf = {
    val conf = super.initSparkConf()
    conf.set("spark.executor.extraLibraryPath", NativeLibLoader.ldPath)
    conf.set("spark.driver.extraLibraryPath", NativeLibLoader.ldPath)
    conf.set("spark.executor.extraJavaOptions", "-Djava.library.path=" + NativeLibLoader.ldPath)
    conf.set("spark.driver.extraJavaOptions", "-Djava.library.path=" + NativeLibLoader.ldPath)
    conf
  }

  def main(args: Array[String]): Unit = {
    setXgbParams(ParamUtils.params)
    val params = ParamUtils.fromArgs(args)
    val isSpark = params.getOrElse("is_spark", true).toString.toBoolean
    val inputPath = params("input_path").toString
    val testInputPath = params.getOrElse("test_input_path", "").toString
    val modelOutputPath = params.getOrElse("model_output_path", "").toString
    val numRound = params.getOrElse("num_round", 1).toString.toInt
    val numFeatures = params.getOrElse("numFeatures", 0).toString.toInt

    if (isSpark) {
      local = params.getOrElse("local", true).toString.toBoolean
      val inputDF = spark.read.format(FED_LIBSVM).option("numFeatures", numFeatures).load(inputPath)
      /*
      import spark.implicits._
      val inputDF = FedMLUtils.loadLibSVMFile(sc, inputPath, numFeatures).map(lp => (lp.label, lp.features))
        .toDF("label", "features")
      */
      println(s"input count=${inputDF.count()}")

      if (testInputPath.nonEmpty) {
        val testInputDF = spark.read.format(FED_LIBSVM).option("numFeatures", numFeatures).load(testInputPath)
        params += "eval_sets" -> Map("test" -> testInputDF)
      }
      val xgbClassifier = new XGBoostClassifier(params.toMap)
        .setNumRound(numRound)

      train(inputDF, modelOutputPath, params, xgbClassifier)
    } else {
      val inputMax = new DMatrix(inputPath)

      trainDMatrix(inputMax, modelOutputPath, params, numRound, testInputPath)
    }
  }

  // noinspection ScalaWeakerAccess
  def trainDMatrix(inputMax: DMatrix, modelOutputPath: String,
                   params: mutable.HashMap[String, Any], numRound: Int = 3,
                   testInputPath: String = ""): Unit = {
    val watches = new mutable.HashMap[String, DMatrix]
    watches += "train" -> inputMax
    if (testInputPath.nonEmpty) {
      val testMax = new DMatrix(testInputPath)
      watches += "test" -> testMax
    }

    // train a model
    val booster = XGBoost.train(inputMax, params.toMap, numRound, watches.toMap)
    // save model to model path
    val file = new File(modelOutputPath)
    if (!file.exists()) {
      file.mkdirs()
    }
    booster.saveModel(file.getAbsolutePath + "/xgb.model.json")
  }

  def train(inputDF: DataFrame, modelOutputPath: String, params: mutable.HashMap[String, Any],
            xgbClassifier: XGBoostClassifier): Unit = {
    // training
    val xgbModel = xgbClassifier.fit(inputDF)
    // output model
    xgbModel.write.overwrite()
      .option("format", params.getOrElse("dump_format", "json").toString)
      .save(modelOutputPath)

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
