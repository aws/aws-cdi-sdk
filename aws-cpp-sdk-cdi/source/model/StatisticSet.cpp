/*
* Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
*  http://aws.amazon.com/apache2.0
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
*/

#include <aws/cdi/model/StatisticSet.h>
#include <aws/core/utils/json/JsonSerializer.h>

#include <utility>

using namespace Aws::Utils::Json;
using namespace Aws::Utils;

namespace Aws
{
namespace CDIMonitoring
{
namespace Model
{

StatisticSet::StatisticSet() : 
    m_maximum(0),
    m_maximumHasBeenSet(false),
    m_minimum(0),
    m_minimumHasBeenSet(false),
    m_sampleCount(0),
    m_sampleCountHasBeenSet(false),
    m_sum(0),
    m_sumHasBeenSet(false)
{
}

StatisticSet::StatisticSet(JsonView jsonValue) : 
    m_maximum(0),
    m_maximumHasBeenSet(false),
    m_minimum(0),
    m_minimumHasBeenSet(false),
    m_sampleCount(0),
    m_sampleCountHasBeenSet(false),
    m_sum(0),
    m_sumHasBeenSet(false)
{
  *this = jsonValue;
}

StatisticSet& StatisticSet::operator =(JsonView jsonValue)
{
  if(jsonValue.ValueExists("maximum"))
  {
    m_maximum = jsonValue.GetInt64("maximum");

    m_maximumHasBeenSet = true;
  }

  if(jsonValue.ValueExists("minimum"))
  {
    m_minimum = jsonValue.GetInt64("minimum");

    m_minimumHasBeenSet = true;
  }

  if(jsonValue.ValueExists("sampleCount"))
  {
    m_sampleCount = jsonValue.GetInt64("sampleCount");

    m_sampleCountHasBeenSet = true;
  }

  if(jsonValue.ValueExists("sum"))
  {
    m_sum = jsonValue.GetInt64("sum");

    m_sumHasBeenSet = true;
  }

  return *this;
}

JsonValue StatisticSet::Jsonize() const
{
  JsonValue payload;

  if(m_maximumHasBeenSet)
  {
   payload.WithInt64("maximum", m_maximum);

  }

  if(m_minimumHasBeenSet)
  {
   payload.WithInt64("minimum", m_minimum);

  }

  if(m_sampleCountHasBeenSet)
  {
   payload.WithInt64("sampleCount", m_sampleCount);

  }

  if(m_sumHasBeenSet)
  {
   payload.WithInt64("sum", m_sum);

  }

  return payload;
}

} // namespace Model
} // namespace CDIMonitoring
} // namespace Aws
