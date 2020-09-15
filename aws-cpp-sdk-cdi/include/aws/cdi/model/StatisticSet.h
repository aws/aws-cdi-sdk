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

#pragma once
#include <aws/cdi/CDIMonitoring_EXPORTS.h>

namespace Aws
{
namespace Utils
{
namespace Json
{
  class JsonValue;
  class JsonView;
} // namespace Json
} // namespace Utils
namespace CDIMonitoring
{
namespace Model
{

  /**
   * Represents a set of statistics that describes a specific metric.<p><h3>See
   * Also:</h3>   <a
   * href="http://docs.aws.amazon.com/goto/WebAPI/cdi-2020-08-13/StatisticSet">AWS
   * API Reference</a></p>
   */
  class AWS_CDIMONITORING_API StatisticSet
  {
  public:
    StatisticSet();
    StatisticSet(Aws::Utils::Json::JsonView jsonValue);
    StatisticSet& operator=(Aws::Utils::Json::JsonView jsonValue);
    Aws::Utils::Json::JsonValue Jsonize() const;


    /**
     * The maximum value of the sample set.
     */
    inline long long GetMaximum() const{ return m_maximum; }

    /**
     * The maximum value of the sample set.
     */
    inline bool MaximumHasBeenSet() const { return m_maximumHasBeenSet; }

    /**
     * The maximum value of the sample set.
     */
    inline void SetMaximum(long long value) { m_maximumHasBeenSet = true; m_maximum = value; }

    /**
     * The maximum value of the sample set.
     */
    inline StatisticSet& WithMaximum(long long value) { SetMaximum(value); return *this;}


    /**
     * The minimum value of the sample set. This must not be greater than maximum.
     */
    inline long long GetMinimum() const{ return m_minimum; }

    /**
     * The minimum value of the sample set. This must not be greater than maximum.
     */
    inline bool MinimumHasBeenSet() const { return m_minimumHasBeenSet; }

    /**
     * The minimum value of the sample set. This must not be greater than maximum.
     */
    inline void SetMinimum(long long value) { m_minimumHasBeenSet = true; m_minimum = value; }

    /**
     * The minimum value of the sample set. This must not be greater than maximum.
     */
    inline StatisticSet& WithMinimum(long long value) { SetMinimum(value); return *this;}


    /**
     * The number of samples used for the statistic set.
     */
    inline long long GetSampleCount() const{ return m_sampleCount; }

    /**
     * The number of samples used for the statistic set.
     */
    inline bool SampleCountHasBeenSet() const { return m_sampleCountHasBeenSet; }

    /**
     * The number of samples used for the statistic set.
     */
    inline void SetSampleCount(long long value) { m_sampleCountHasBeenSet = true; m_sampleCount = value; }

    /**
     * The number of samples used for the statistic set.
     */
    inline StatisticSet& WithSampleCount(long long value) { SetSampleCount(value); return *this;}


    /**
     * The sum of values for the sample set.
     */
    inline long long GetSum() const{ return m_sum; }

    /**
     * The sum of values for the sample set.
     */
    inline bool SumHasBeenSet() const { return m_sumHasBeenSet; }

    /**
     * The sum of values for the sample set.
     */
    inline void SetSum(long long value) { m_sumHasBeenSet = true; m_sum = value; }

    /**
     * The sum of values for the sample set.
     */
    inline StatisticSet& WithSum(long long value) { SetSum(value); return *this;}

  private:

    long long m_maximum;
    bool m_maximumHasBeenSet;

    long long m_minimum;
    bool m_minimumHasBeenSet;

    long long m_sampleCount;
    bool m_sampleCountHasBeenSet;

    long long m_sum;
    bool m_sumHasBeenSet;
  };

} // namespace Model
} // namespace CDIMonitoring
} // namespace Aws
