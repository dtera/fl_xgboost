package ml.dmlc.xgboost4j.java.app

import org.apache.spark.{SparkConf, SparkContext}
import org.apache.spark.sql.{SQLContext, SparkSession}

trait AbstractSparkApp {
  lazy implicit val spark: SparkSession = {
    val sparkBuild = SparkSession
      .builder
      .appName(appName)
      .config(initSparkConf())
    if (isLocal) {
      sparkBuild.master("local[*]")
    }
    if (enableHiveSupport) {
      sparkBuild.enableHiveSupport()
    }
    sparkBuild.getOrCreate()
  }

  lazy implicit val sc: SparkContext = spark.sparkContext
  lazy implicit val sqc: SQLContext = spark.sqlContext

  def appName: String = this.getClass.getSimpleName

  def isLocal: Boolean = true

  def enableHiveSupport: Boolean = false

  def initSparkConf(): SparkConf = new SparkConf()

}
