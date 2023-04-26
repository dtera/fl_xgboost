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

import org.apache.commons.lang.math.NumberUtils

import java.util
import scala.collection.mutable

object ParamUtils {
  private val NO_VALUE_KEY = "__NO_VALUE_KEY"
  val params = new mutable.HashMap[String, Any]()

  def fromArgs(args: Array[String]): mutable.HashMap[String, Any] = {
    if (args.length > 0 && args(0).contains("=")) fromArgs(args, "=")
    else if (args.length > 0 && args(0).contains(":")) fromArgs(args, ":")
    else fromUnixArgs(args)
  }

  def fromArgs(args: Array[String], sep: String): mutable.HashMap[String, Any] = {
    for (arg <- args) {
      if (!arg.contains(sep)) {
        throw new IllegalArgumentException(String.format("Error parsing arguments '%s' on '%s'. " +
          "Please make sure args with = in them.", util.Arrays.asList(args), arg))
      }
      val splits: Array[String] = arg.split(sep, -1)
      params.put(splits(0), splits(1))
    }
    params
  }

  private def fromUnixArgs(args: Array[String]): mutable.HashMap[String, Any] = {
    var i = 0
    while (i < args.length) {
      var key: String = null
      if (args(i).startsWith("--")) key = args(i).substring(2)
      else if (args(i).startsWith("-")) key = args(i).substring(1)
      else {
        throw new IllegalArgumentException(
          String.format("Error parsing arguments '%s' on '%s'. " +
            "Please prefix keys with -- or -.", util.Arrays.asList(args), args(i)))
      }
      if (key.isEmpty) throw new IllegalArgumentException("The input " +
        util.Arrays.asList(args) + " contains an empty argument")
      i += 1 // try to find the value

      if (i >= args.length) params.put(key, NO_VALUE_KEY)
      else if (NumberUtils.isNumber(args(i))) {
        params.put(key, args(i))
        i += 1
      }
      else if (args(i).startsWith("--") || args(i).startsWith("-")) {
        // the argument cannot be a negative number because we checked earlier
        // -> the next argument is a parameter name
        params.put(key, NO_VALUE_KEY)
      }
      else {
        params.put(key, args(i))
        i += 1
      }
    }
    params
  }

}
