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

import ml.dmlc.xgboost4j.scala.fl.XGBClassifierRunner.{defaultXgbParams, evaluator}
import ml.dmlc.xgboost4j.scala.fl.util.XgbUtils.{checkPredicts, saveDumpModel}
import ml.dmlc.xgboost4j.scala.fl.test.SparkTest
import ml.dmlc.xgboost4j.scala.spark.{XGBoostClassificationModel, XGBoostClassifier}
import ml.dmlc.xgboost4j.scala.{DMatrix, XGBoost}
import org.apache.spark.ml.{Pipeline, PipelineModel}
import org.apache.spark.ml.evaluation.MulticlassClassificationEvaluator
import org.apache.spark.ml.feature.{IndexToString, StringIndexer, VectorAssembler}
import org.apache.spark.ml.tuning.{CrossValidator, ParamGridBuilder}
import org.apache.spark.ml.util.FedMLUtils.FED_LIBSVM
import org.apache.spark.sql.types.{DoubleType, StringType, StructField, StructType}

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
    val trainInput = spark.read.format(FED_LIBSVM).load("../data/a9a.train")
    val testInput = spark.read.format(FED_LIBSVM).load("../data/a9a.test")

    params += "fl_on" -> 0
    params += "fl_comm_type" -> "none"
    // params += "num_workers" -> 2
    // params += "timeout_request_workers" -> 60000L
    params += "eval_sets" -> Map("test" -> testInput)

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

  "spark xgb" should "work with iris dataset" in {
    val inputPath = "../data/iris/iris.data"

    // Load dataset
    val schema = new StructType(Array(
      StructField("sepal length", DoubleType, true),
      StructField("sepal width", DoubleType, true),
      StructField("petal length", DoubleType, true),
      StructField("petal width", DoubleType, true),
      StructField("class", StringType, true)))

    val rawInput = spark.read.schema(schema).csv(inputPath)
    val labelIndexer = new StringIndexer()
      .setInputCol("class")
      .setOutputCol("label")
      .fit(rawInput)
    val assembler = new VectorAssembler()
      .setInputCols(Array("sepal length", "sepal width", "petal length", "petal width"))
      .setOutputCol("features")
    var input = labelIndexer.transform(rawInput) // .drop(schema.fieldNames: _*)
    input = assembler.transform(input)
    val Array(training, test) = input.randomSplit(Array(0.8, 0.2), 123)

    params += "fl_on" -> 0
    params += "fl_comm_type" -> "none"
    params += "num_workers" -> 2
    params += "timeout_request_workers" -> 60000L
    params += "eval_sets" -> Map("train" -> training, "test" -> test)
    params += "objective" -> "multi:softprob"
    params += "num_class" -> 3

    val xgbClassifier = new XGBoostClassifier(params.toMap).setMissing(0.0f).setNumRound(3)
    val xgbModel = xgbClassifier.fit(training)

    val testAUC = evaluator.evaluate(xgbModel.transform(test))
    println(s"Test AUC: $testAUC")
  }

  "spark xgb ppl" should "work with iris dataset" in {
    val inputPath = "../data/iris/iris.data"
    val nativeModelPath = "./model/iris"
    val pipelineModelPath = "./model/iris.ppl"

    // Load dataset
    val schema = new StructType(Array(
      StructField("sepal length", DoubleType, true),
      StructField("sepal width", DoubleType, true),
      StructField("petal length", DoubleType, true),
      StructField("petal width", DoubleType, true),
      StructField("class", StringType, true)))

    val rawInput = spark.read.schema(schema).csv(inputPath)

    // Split training and test dataset
    val Array(training, test) = rawInput.randomSplit(Array(0.8, 0.2), 123)

    params += "fl_on" -> 0
    params += "fl_comm_type" -> "none"
    params += "num_workers" -> 1
    params += "timeout_request_workers" -> 60000L
    params += "objective" -> "multi:softprob"
    params += "num_class" -> 3

    // Build ML pipeline, it includes 4 stages:
    // 1, Assemble all features into a single vector column.
    // 2, From string label to indexed double label.
    // 3, Use XGBoostClassifier to train classification model.
    // 4, Convert indexed double label back to original string label.
    val assembler = new VectorAssembler()
      .setInputCols(Array("sepal length", "sepal width", "petal length", "petal width"))
      .setOutputCol("features")
    val labelIndexer = new StringIndexer()
      .setInputCol("class")
      .setOutputCol("classIndex")
      .fit(training)
    val booster = new XGBoostClassifier(params.toMap).setMissing(0.0f)
      .setFeaturesCol("features")
      .setLabelCol("classIndex")
    val labelConverter = new IndexToString()
      .setInputCol("prediction")
      .setOutputCol("realLabel")
      .setLabels(labelIndexer.labels)

    val pipeline = new Pipeline()
      .setStages(Array(assembler, labelIndexer, booster, labelConverter))
    val model = pipeline.fit(training)

    // Batch prediction
    val prediction = model.transform(test)
    prediction.show(false)

    // Model evaluation
    val evaluator = new MulticlassClassificationEvaluator()
    evaluator.setLabelCol("classIndex")
    evaluator.setPredictionCol("prediction")
    val accuracy = evaluator.evaluate(prediction)
    println("The model accuracy is : " + accuracy)

    // Tune model using cross validation
    val paramGrid = new ParamGridBuilder()
      .addGrid(booster.maxDepth, Array(3, 8))
      .addGrid(booster.eta, Array(0.2, 0.6))
      .build()
    val cv = new CrossValidator()
      .setEstimator(pipeline)
      .setEvaluator(evaluator)
      .setEstimatorParamMaps(paramGrid)
      .setNumFolds(3)

    val cvModel = cv.fit(training)

    val bestModel = cvModel.bestModel.asInstanceOf[PipelineModel].stages(2)
      .asInstanceOf[XGBoostClassificationModel]
    println("The params of best XGBoostClassification model : " +
      bestModel.extractParamMap())
    println("The training summary of best XGBoostClassificationModel : " +
      bestModel.summary)

    // Export the XGBoostClassificationModel as local XGBoost model,
    // then you can load it back in local Python environment.
    bestModel.nativeBooster.saveModel(nativeModelPath)

    // ML pipeline persistence
    model.write.overwrite().save(pipelineModelPath)

    // Load a saved model and serving
    val model2 = PipelineModel.load(pipelineModelPath)
    model2.transform(test).show(false)
  }

}
