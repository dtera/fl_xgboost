/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package org.apache.spark.ml.source.libsvm

import org.apache.hadoop.conf.Configuration
import org.apache.hadoop.fs.FileStatus
import org.apache.spark.TaskContext
import org.apache.spark.ml.attribute.AttributeGroup
import org.apache.spark.ml.feature.LabeledPoint
import org.apache.spark.ml.linalg.{VectorUDT, Vectors}
import org.apache.spark.ml.util.FedMLUtils
import org.apache.spark.mllib.util.MLUtils
import org.apache.spark.sql.catalyst.InternalRow
import org.apache.spark.sql.catalyst.encoders.RowEncoder
import org.apache.spark.sql.catalyst.expressions.AttributeReference
import org.apache.spark.sql.catalyst.expressions.codegen.GenerateUnsafeProjection
import org.apache.spark.sql.execution.datasources.{HadoopFileLinesReader, PartitionedFile}
import org.apache.spark.sql.sources.Filter
import org.apache.spark.sql.types.{DataTypes, DoubleType, MetadataBuilder, StructField, StructType}
import org.apache.spark.sql.{Row, SparkSession}
import org.apache.spark.util.SerializableConfiguration

import java.io.IOException

class FedLibSVMDataSource private() {}

private[libsvm] class FedLibSVMFileFormat extends LibSVMFileFormat {
  override def shortName(): String = "fedlibsvm"

  override def toString: String = "FedLibSVM"

  protected def verifySchema(dataSchema: StructType, forWriting: Boolean): Unit = {
    if (
      dataSchema.size != 2 ||
        !dataSchema.head.dataType.sameType(DataTypes.DoubleType) ||
        !dataSchema(1).dataType.sameType(new VectorUDT()) ||
        !(forWriting || dataSchema(1).metadata.getLong(LibSVMOptions.NUM_FEATURES).toInt > 0)
    ) {
      throw new IOException(s"Illegal schema for libsvm data, schema=$dataSchema")
    }
  }

  override def inferSchema(
                            sparkSession: SparkSession,
                            options: Map[String, String],
                            files: Seq[FileStatus]): Option[StructType] = {
    val libSVMOptions = new LibSVMOptions(options)
    val numFeatures: Int = libSVMOptions.numFeatures.getOrElse {
      require(files.nonEmpty, "No input path specified for libsvm data")
      logWarning(
        "'numFeatures' option not specified, determining the number of features by going " +
          "though the input. If you know the number in advance, please specify it via " +
          "'numFeatures' option to avoid the extra scan.")

      val paths = files.map(_.getPath.toUri.toString)
      val parsed = FedMLUtils.parseLibSVMFile(sparkSession, paths)
      MLUtils.computeNumFeatures(parsed)
    }

    val labelField = StructField("label", DoubleType, nullable = false)

    val extraMetadata = new MetadataBuilder()
      .putLong(LibSVMOptions.NUM_FEATURES, numFeatures)
      .build()
    val attrGroup = new AttributeGroup(name = "features", numAttributes = numFeatures)
    val featuresField = attrGroup.toStructField(extraMetadata)

    Some(StructType(labelField :: featuresField :: Nil))
  }

  override def buildReader(
                            sparkSession: SparkSession,
                            dataSchema: StructType,
                            partitionSchema: StructType,
                            requiredSchema: StructType,
                            filters: Seq[Filter],
                            options: Map[String, String],
                            hadoopConf: Configuration): PartitionedFile => Iterator[InternalRow] = {
    verifySchema(dataSchema, false)
    val numFeatures = dataSchema("features").metadata.getLong(LibSVMOptions.NUM_FEATURES).toInt
    assert(numFeatures > 0)

    val libSVMOptions = new LibSVMOptions(options)
    val isSparse = libSVMOptions.isSparse

    val broadcastedHadoopConf =
      sparkSession.sparkContext.broadcast(new SerializableConfiguration(hadoopConf))

    (file: PartitionedFile) => {
      val linesReader = new HadoopFileLinesReader(file, broadcastedHadoopConf.value.value)
      Option(TaskContext.get()).foreach(_.addTaskCompletionListener[Unit](_ => linesReader.close()))

      val points = linesReader
        .map(_.toString.trim)
        .filterNot(line => line.isEmpty || line.startsWith("#"))
        .map { line =>
          val (label, indices, values) = FedMLUtils.parseLibSVMRecord(line)
          LabeledPoint(label, Vectors.sparse(numFeatures, indices, values))
        }

      val toRow = RowEncoder(dataSchema).createSerializer()
      val fullOutput = dataSchema.map { f =>
        AttributeReference(f.name, f.dataType, f.nullable, f.metadata)()
      }
      val requiredOutput = fullOutput.filter { a =>
        requiredSchema.fieldNames.contains(a.name)
      }

      val requiredColumns = GenerateUnsafeProjection.generate(requiredOutput, fullOutput)

      points.map { pt =>
        val features = if (isSparse) pt.features.toSparse else pt.features.toDense
        requiredColumns(toRow(Row(pt.label, features)))
      }
    }
  }
}
