package ml.dmlc.xgboost4j.scala.fl.util

import java.io.PrintWriter
import java.util

object XgbUtils {
  def checkPredicts(fPredicts: Array[Array[Float]], sPredicts: Array[Array[Float]]): Boolean = {
    if (fPredicts.length != sPredicts.length) return false
    for (i <- fPredicts.indices) {
      if (!util.Arrays.equals(fPredicts(i), sPredicts(i))) return false
    }
    true
  }

  def saveDumpModel(modelPath: String, modelInfos: Array[String]): Unit = {
    try {
      val writer = new PrintWriter(modelPath, "UTF-8")
      for (i <- modelInfos.indices) {
        writer.print("booster[" + i + "]:\n")
        writer.print(modelInfos(i))
      }
      writer.close()
    } catch {
      case e: Exception =>
        e.printStackTrace()
    }
  }
}
