/*
 * Copyright 2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/core/utils/StringUtils.h>
#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/core/utils/logging/LogMacros.h>
#include <aws/monitoring/CloudWatchClient.h>
#include <aws/monitoring/model/PutMetricDataRequest.h>
#include <aws_common/sdk_utils/client_configuration_provider.h>
#include <aws_ros1_common/sdk_utils/logging/aws_ros_logger.h>
#include <aws_ros1_common/sdk_utils/ros1_node_parameter_reader.h>
#include <ros/ros.h>
#include <ros_monitoring_msgs/MetricData.h>
#include <ros_monitoring_msgs/MetricDimension.h>
#include <ros_monitoring_msgs/MetricList.h>
#include <std_msgs/String.h>

#include <cloudwatch_metrics_collector/metrics_collector.hpp>
#include <cloudwatch_metrics_common/metric_service.hpp>
#include <cloudwatch_metrics_common/metric_service_factory.hpp>
#include <string>
#include <vector>
#include <map>

#include <cloudwatch_metrics_collector/metrics_collector_parameter_helper.hpp>

using namespace Aws::Client;
using namespace Aws::Utils::Logging;
using namespace Aws::CloudWatchMetrics::Utils;

namespace Aws {
namespace CloudWatchMetrics {
namespace Utils {

void MetricsCollector::Initialize(std::string metric_namespace,
                         std::map<std::string, std::string> & default_dimensions,
                         int storage_resolution,
                         ros::NodeHandle node_handle,
                         const Aws::Client::ClientConfiguration & config,
                         const Aws::SDKOptions & sdk_options,
                         std::shared_ptr<MetricServiceFactory> metric_service_factory) {

  this->metric_namespace_ = metric_namespace;
  this->default_dimensions_ = default_dimensions;
  this->storage_resolution_.store(storage_resolution);
  this->node_handle_ = node_handle;
  this->metric_service_ = metric_service_factory->createMetricService(this->metric_namespace_,
                                                                      config,
                                                                      sdk_options);
}

void MetricsCollector::SubscribeAllTopics()
{
  ReadTopics(topics_);
  for (auto it = topics_.begin(); it != topics_.end(); ++it) {
    ros::Subscriber sub = node_handle_.subscribe<ros_monitoring_msgs::MetricList>(
            *it, kNodeSubQueueSize,
            [this](const ros_monitoring_msgs::MetricList::ConstPtr & metric_list_msg) -> void {
              printf("recording metric\n");
                this->RecordMetrics(metric_list_msg);
            });
    subscriptions_.push_back(sub);
  }
}

void MetricsCollector::RecordMetrics(
  const ros_monitoring_msgs::MetricList::ConstPtr & metric_list_msg)
{
  AWS_LOGSTREAM_DEBUG(__func__, "Received " << metric_list_msg->metrics.size() << " metrics");

  for (auto metric_msg = metric_list_msg->metrics.begin();
       metric_msg != metric_list_msg->metrics.end(); ++metric_msg) {

    std::map<std::string, std::string> dimensions;

    for (auto it = default_dimensions_.begin(); it != default_dimensions_.end(); ++it) {
      dimensions.emplace(it->first, it->second);  // ignore the return, if we get a duplicate we're
                                                  // going to stick with the first one
    }
    for (auto it = metric_msg->dimensions.begin(); it != metric_msg->dimensions.end(); ++it) {
      dimensions.emplace((*it).name, (*it).value);  // ignore the return, if we get a duplicate
                                                    // we're going to stick with the first one
    }
    AWS_LOGSTREAM_DEBUG(__func__, "Recording metric with name=[" << metric_msg->metric_name << "]");

    // create a MetricObject with message parameters to batch
    Aws::CloudWatchMetrics::Utils::MetricObject metric_object {metric_msg->metric_name,
                                                              metric_msg->value,
                                                              metric_msg->unit,
                                                              GetMetricDataEpochMillis(*metric_msg),
                                                              dimensions,
                                                              this->storage_resolution_.load()};
    bool batched = metric_service_->batchData(metric_object);

    if (!batched) {
      AWS_LOGSTREAM_ERROR(__func__, "Failed to record metric");
    }
  }
}

int64_t MetricsCollector::GetMetricDataEpochMillis(const ros_monitoring_msgs::MetricData & metric_msg)
{
  return metric_msg.time_stamp.toNSec() / 1000000;
}

void MetricsCollector::TriggerPublish(const ros::TimerEvent &)
{
  AWS_LOG_DEBUG(__func__, "Flushing metrics");
  printf("triggerpung publish\n");
  this->metric_service_->publishBatchedData();
}

bool MetricsCollector::start() {

  this->SubscribeAllTopics();

  if (this->metric_service_) {
    return this->metric_service_->start();
  }

  return false;
}

bool MetricsCollector::shutdown() {

  if (this->metric_service_) {
    return this->metric_service_->shutdown();
  }
  return false;
}

}  // namespace Utils
}  // namespace CloudWatchMetrics
}  // namespace Aws


