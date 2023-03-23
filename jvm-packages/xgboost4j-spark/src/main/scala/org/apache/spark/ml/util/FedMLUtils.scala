package org.apache.spark.ml.util

import org.apache.spark.SparkContext
import org.apache.spark.annotation.Since
import org.apache.spark.internal.Logging
import org.apache.spark.mllib.linalg._
import org.apache.spark.mllib.regression.LabeledPoint
import org.apache.spark.mllib.util.MLUtils
import org.apache.spark.rdd.RDD
import org.apache.spark.sql.SparkSession
import org.apache.spark.sql.execution.datasources.DataSource
import org.apache.spark.sql.execution.datasources.text.TextFileFormat
import org.apache.spark.sql.functions._
import org.apache.spark.storage.StorageLevel

/**
 * Helper methods to load, save and pre-process data used in federated learning.
 */
object FedMLUtils extends Logging {

  /**
   * Loads labeled data in the LIBSVM format into an RDD[LabeledPoint].
   * The LIBSVM format is a text-based format used by LIBSVM and LIBLINEAR.
   * Each line represents a labeled sparse feature vector using the following format:
   * {{{[@SAMPLE_ID] [LABEL[#WEIGHT]] index1:value1 index2:value2 ...}}}
   * where the indices are one-based and in ascending order.
   * This method parses each line into a [[org.apache.spark.mllib.regression.LabeledPoint]],
   * where the feature indices are converted to zero-based.
   *
   * @param sc            Spark context
   * @param path          file or directory path in any Hadoop-supported file system URI
   * @param numFeatures   number of features, which will be determined from the input data if a
   *                      nonpositive value is given.
   *                      This is useful when the dataset is already split
   *                      into multiple files and you want to load them separately, because some
   *                      features may not present in certain files, which leads to inconsistent
   *                      feature dimensions.
   * @param minPartitions min number of partitions
   * @return labeled data stored as an RDD[LabeledPoint]
   */
  @Since("1.0.0")
  def loadLibSVMFile(
                      sc: SparkContext,
                      path: String,
                      numFeatures: Int,
                      minPartitions: Int): RDD[LabeledPoint] = {
    val parsed = parseLibSVMFile(sc, path, minPartitions)

    // Determine number of features.
    val d = if (numFeatures > 0) {
      numFeatures
    } else {
      parsed.persist(StorageLevel.MEMORY_ONLY)
      MLUtils.computeNumFeatures(parsed)
    }

    parsed.map { case (label, indices, values) =>
      LabeledPoint(label, Vectors.sparse(d, indices, values))
    }
  }

  private[spark] def parseLibSVMFile(sc: SparkContext, path: String, minPartitions: Int):
  RDD[(Double, Array[Int], Array[Double])] = {
    sc.textFile(path, minPartitions)
      .map(_.trim)
      .filter(line => !(line.isEmpty || line.startsWith("#")))
      .map(parseLibSVMRecord)
  }

  private[spark] def parseLibSVMFile(sparkSession: SparkSession, paths: Seq[String]):
  RDD[(Double, Array[Int], Array[Double])] = {
    val lines = sparkSession.baseRelationToDataFrame(
      DataSource.apply(
        sparkSession,
        paths = paths,
        className = classOf[TextFileFormat].getName
      ).resolveRelation(checkFilesExist = false))
      .select("value")

    import lines.sqlContext.implicits._

    lines.select(trim($"value").as("line"))
      .filter(not((length($"line") === 0).or($"line".startsWith("#"))))
      .as[String]
      .rdd
      .map(parseLibSVMRecord)
  }

  private[spark] def parseLibSVMRecord(line: String): (Double, Array[Int], Array[Double]) = {
    var items = line.split(' ')
    var head = items.head
    var sampleId = ""
    if (head.startsWith("@")) {
      sampleId = head.stripPrefix("@")
      items = items.tail
      head = items.head
    }
    var features = items.tail
    val label = if (head.contains(":")) {
      features = items
      -1
    } else {
      val i = head.indexOf("#")
      (if (i == -1) head else head.substring(0, i)).toDouble
    }
    val (indices, values) = features.filter(_.nonEmpty).map { item =>
      val indexAndValue = item.split(':')
      val index = indexAndValue(0).toInt - 1 // Convert 1-based indices to 0-based.
      val value = indexAndValue(1).toDouble
      (index, value)
    }.unzip

    // check if indices are one-based and in ascending order
    var previous = -1
    var i = 0
    val indicesLength = indices.length
    while (i < indicesLength) {
      val current = indices(i)
      require(current > previous, s"indices should be one-based and in ascending order;"
        + s""" found current=$current, previous=$previous; line="$line"""")
      previous = current
      i += 1
    }
    (label, indices, values)
  }

  /**
   * Loads labeled data in the LIBSVM format into an RDD[LabeledPoint], with the default number of
   * partitions.
   */
  @Since("1.0.0")
  def loadLibSVMFile(
                      sc: SparkContext,
                      path: String,
                      numFeatures: Int): RDD[LabeledPoint] =
    loadLibSVMFile(sc, path, numFeatures, sc.defaultMinPartitions)

  /**
   * Loads binary labeled data in the LIBSVM format into an RDD[LabeledPoint], with number of
   * features determined automatically and the default number of partitions.
   */
  @Since("1.0.0")
  def loadLibSVMFile(sc: SparkContext, path: String): RDD[LabeledPoint] =
    loadLibSVMFile(sc, path, -1)
}
