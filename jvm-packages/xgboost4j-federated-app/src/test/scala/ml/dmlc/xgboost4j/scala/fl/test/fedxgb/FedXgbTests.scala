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
package ml.dmlc.xgboost4j.scala.fl.test.fedxgb

import ml.dmlc.xgboost4j.scala.fl.XGBClassifierRunner.{evaluator, defaultXgbParams}
import ml.dmlc.xgboost4j.scala.fl.util.XgbUtils.{checkPredicts, saveDumpModel}
import ml.dmlc.xgboost4j.scala.fl.test.SparkTest
import ml.dmlc.xgboost4j.scala.spark.XGBoostClassifier
import ml.dmlc.xgboost4j.scala.{DMatrix, XGBoost}
import org.apache.spark.ml.util.FedMLUtils.FED_LIBSVM

import java.io.File
import scala.collection.mutable

class FedXgbTests extends SparkTest {

  val params = new mutable.HashMap[String, Any]()
  defaultXgbParams(params)

  "[guest]fed spark xgb" should "work with a9a dataset" in {
    val trainInput = spark.read.format(FED_LIBSVM).load("../data/a9a.guest.train")
    val testInput = spark.read.format(FED_LIBSVM).load("../data/a9a.guest.test")
    trainInput.show(10)

    params += "fl_port" -> "30002"
    params += "fl_role" -> "guest"
    params += "fl_pulsar_topic_prefix" -> "federated_spark_xgb_a9a_"
    params += "fl_part_id" -> 0
    params += "eval_sets" -> Map("test" -> testInput)

    val xgbClassifier = new XGBoostClassifier(params.toMap).setNumRound(3)
    val xgbModel = xgbClassifier.fit(trainInput)
    val modelDir = "./model/a9a.guest.xgb.spark"
    xgbModel.write.overwrite().option("format", "json").save(modelDir)

    // val testAUC = evaluator.evaluate(xgbModel.transform(testInput))
    // println(s"Test AUC: $testAUC")
  }

  "[host]fed spark xgb" should "work with a9a dataset" in {
    val trainInput = spark.read.format(FED_LIBSVM).load("../data/a9a.host.train")
    val testInput = spark.read.format(FED_LIBSVM).load("../data/a9a.host.test")
    trainInput.show(10)

    params += "fl_address" -> "0.0.0.0:30002"
    params += "fl_role" -> "host"
    params += "fl_pulsar_topic_prefix" -> "federated_spark_xgb_a9a_"
    params += "fl_part_id" -> 1
    params += "eval_sets" -> Map("test" -> testInput)

    val xgbClassifier = new XGBoostClassifier(params.toMap).setNumRound(3)
    val xgbModel = xgbClassifier.fit(trainInput)
    val modelDir = "./model/a9a.host.xgb.spark"
    xgbModel.write.overwrite().option("format", "json").save(modelDir)

    // val testAUC = evaluator.evaluate(xgbModel.transform(testInput))
    // println(s"Test AUC: $testAUC")
  }

  "[guest]fed xgb" should "work with a9a dataset" in {
    val trainMax = new DMatrix("../data/a9a.guest.train?format=libsvm")
    val testMax = new DMatrix("../data/a9a.guest.test?format=libsvm")

    params += "fl_port" -> "30002"
    params += "fl_role" -> "guest"
    params += "fl_pulsar_topic_prefix" -> "federated_xgb_a9a_"
    params += "fl_bit_len" -> 1024
    params += "fl_part_id" -> 0

    val watches = new mutable.HashMap[String, DMatrix]
    watches += "train" -> trainMax
    watches += "test" -> testMax

    val round = 3
    // train a model
    val booster = XGBoost.train(trainMax, params.toMap, round, watches.toMap)
    // save model to model path
    val file = new File("./model/a9a.guest")
    if (!file.exists()) {
      file.mkdirs()
    }
    booster.saveModel(file.getAbsolutePath + "/xgb.model.json")
    // save dmatrix into binary buffer
    testMax.saveBinary(file.getAbsolutePath + "/dtest.buffer")
  }

  "[host]fed xgb" should "work with a9a dataset" in {
    val trainMax = new DMatrix("../data/a9a.host.train?format=libsvm")
    val testMax = new DMatrix("../data/a9a.host.test?format=libsvm")

    params += "fl_address" -> "0.0.0.0:30002"
    params += "fl_role" -> "host"
    params += "fl_pulsar_topic_prefix" -> "federated_xgb_a9a_"
    params += "fl_part_id" -> 1

    val watches = new mutable.HashMap[String, DMatrix]
    watches += "train" -> trainMax
    watches += "test" -> testMax

    val round = 3
    // train a model
    val booster = XGBoost.train(trainMax, params.toMap, round, watches.toMap)
    // save model to model path
    val file = new File("./model/a9a.host")
    if (!file.exists()) {
      file.mkdirs()
    }
    booster.saveModel(file.getAbsolutePath + "/xgb.model.json")
    // save dmatrix into binary buffer
    testMax.saveBinary(file.getAbsolutePath + "/dtest.buffer")
  }

  "spark xgb" should "work with a9a dataset" in {
    val trainInput = spark.read.format("libsvm").load("../data/a9a.train")
    val testInput = spark.read.format("libsvm").load("../data/a9a.test")

    params += "fl_on" -> 0
    params += "fl_comm_type" -> "none"
    // params += "num_workers" -> 2
    // params += "timeout_request_workers" -> 60000L

    val xgbClassifier = new XGBoostClassifier(params.toMap).setMissing(0.0f)
    val xgbModel = xgbClassifier.fit(trainInput)
    val testAUC = evaluator.evaluate(xgbModel.transform(testInput))
    println(s"Test AUC: $testAUC")
  }

  "xgb" should "work with a9a dataset" in {
    val trainMax = new DMatrix("../data/a9a.train?format=libsvm")
    val testMax = new DMatrix("../data/a9a.test?format=libsvm")

    val watches = new mutable.HashMap[String, DMatrix]
    watches += "train" -> trainMax
    watches += "test" -> testMax

    params += "fl_on" -> 0
    params += "fl_comm_type" -> "none"
    val round = 3
    // train a model
    val booster = XGBoost.train(trainMax, params.toMap, round, watches.toMap)
    // predict
    val predicts = booster.predict(testMax)
    // save model to model path
    val file = new File("./model/a9a")
    if (!file.exists()) {
      file.mkdirs()
    }
    booster.saveModel(file.getAbsolutePath + "/xgb.model.json")
    // save dmatrix into binary buffer
    testMax.saveBinary(file.getAbsolutePath + "/dtest.buffer")

    // reload model and data
    val booster2 = XGBoost.loadModel(file.getAbsolutePath + "/xgb.model.json")
    val testMax2 = new DMatrix(file.getAbsolutePath + "/dtest.buffer")
    val predicts2 = booster2.predict(testMax2)

    // check predicts
    println(checkPredicts(predicts, predicts2))
  }

  "xgb" should "work with agaricus dataset" in {
    val trainMax = new DMatrix("../demo/data/agaricus.txt.train")
    val testMax = new DMatrix("../demo/data/agaricus.txt.test")

    val watches = new mutable.HashMap[String, DMatrix]
    watches += "train" -> trainMax
    watches += "test" -> testMax

    params += "fl_on" -> 0
    params += "fl_comm_type" -> "none"
    val round = 3
    // train a model
    val booster = XGBoost.train(trainMax, params.toMap, round, watches.toMap)
    // predict
    val predicts = booster.predict(testMax)
    // save model to model path
    val file = new File("./model/agaricus")
    if (!file.exists()) {
      file.mkdirs()
    }
    booster.saveModel(file.getAbsolutePath + "/xgb.model.json")
    // dump model with feature map
    val modelInfos = booster.getModelDump("../demo/data/featmap.txt")
    saveDumpModel(file.getAbsolutePath + "/dump.raw.txt", modelInfos)
    // save dmatrix into binary buffer
    testMax.saveBinary(file.getAbsolutePath + "/dtest.buffer")

    // reload model and data
    val booster2 = XGBoost.loadModel(file.getAbsolutePath + "/xgb.model.json")
    val testMax2 = new DMatrix(file.getAbsolutePath + "/dtest.buffer")
    val predicts2 = booster2.predict(testMax2)

    // check predicts
    println(checkPredicts(predicts, predicts2))
  }

}
