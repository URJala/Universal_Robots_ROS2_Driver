// Copyright 2025, Universal Robots A/S
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in the
//      documentation and/or other materials provided with the distribution.
//
//    * Neither the name of the {copyright_holder} nor the names of its
//      contributors may be used to endorse or promote products derived from
//      this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

//----------------------------------------------------------------------
/*!\file
 *
 * \author  Jacob Larsen jala@universal-robots.com
 * \date    2025-01-07
 *
 *
 *
 *
 */
//----------------------------------------------------------------------

#include <ur_controllers/tool_contact_controller.hpp>
#include "controller_interface/helpers.hpp"
#include <rclcpp/rclcpp.hpp>
#include <rclcpp/logging.hpp>
#include "std_msgs/msg/bool.hpp"
#include <lifecycle_msgs/msg/state.hpp>

namespace ur_controllers
{

controller_interface::CallbackReturn ToolContactController::on_init()
{
  tool_contact_param_listener_ = std::make_shared<tool_contact_controller::ParamListener>(get_node());
  tool_contact_params_ = tool_contact_param_listener_->get_params();

  // Resize this value depending on reference interfaces to be sent
  reference_interfaces_.resize(1, std::numeric_limits<double>::quiet_NaN());
  return controller_interface::CallbackReturn::SUCCESS;
}

controller_interface::CallbackReturn
ToolContactController::on_configure(const rclcpp_lifecycle::State& /* previous_state */)
{
  tool_contact_action_server_ = rclcpp_action::create_server<ur_msgs::action::ToolContact>(
      get_node(), std::string(get_node()->get_name()) + "/enable_tool_contact",
      std::bind(&ToolContactController::goal_received_callback, this, std::placeholders::_1, std::placeholders::_2),
      std::bind(&ToolContactController::goal_cancelled_callback, this, std::placeholders::_1),
      std::bind(&ToolContactController::goal_accepted_callback, this, std::placeholders::_1));

  return controller_interface::CallbackReturn::SUCCESS;
}

controller_interface::InterfaceConfiguration ToolContactController::command_interface_configuration() const
{
  controller_interface::InterfaceConfiguration config;
  config.type = controller_interface::interface_configuration_type::INDIVIDUAL;
  const std::string tf_prefix = tool_contact_params_.tf_prefix;

  // Start or stop tool contact functionality
  config.names.emplace_back(tf_prefix + "tool_contact/tool_contact_status");
  return config;
}

controller_interface::InterfaceConfiguration ToolContactController::state_interface_configuration() const
{
  controller_interface::InterfaceConfiguration config;
  config.type = controller_interface::interface_configuration_type::INDIVIDUAL;

  const std::string tf_prefix = tool_contact_params_.tf_prefix;
  config.names.push_back(tf_prefix + "tool_contact/tool_contact_result");
  config.names.push_back(tf_prefix + "get_robot_software_version/get_version_major");
  return config;
}

controller_interface::CallbackReturn
ToolContactController::on_activate(const rclcpp_lifecycle::State& /* previous_state */)
{
  /*Figure out how to get reference interface by name*/
  // const std::string interface_name = tool_contact_params_.tf_prefix + get_node()->get_name() +
  // "tool_contact/enable"; auto it = std::find_if(reference_interfaces_.begin(), reference_interfaces_.end(),
  //                        [&](auto& interface) { return (interface.get_name() == interface_name); });
  // if (it != reference_interfaces_.end()) {
  //   reference_interface_ = *it;
  //   reference_interface_->get().set_value(0.0);
  // } else {
  //   RCLCPP_ERROR(get_node()->get_logger(), "Did not find '%s' in command interfaces.",
  //   interface_name.c_str()); return controller_interface::CallbackReturn::ERROR;
  // }
  reference_interfaces_[0] = 0.0;
  {
    const std::string interface_name = tool_contact_params_.tf_prefix + "tool_contact/tool_contact_status";
    auto it = std::find_if(command_interfaces_.begin(), command_interfaces_.end(),
                           [&](auto& interface) { return (interface.get_name() == interface_name); });
    if (it != command_interfaces_.end()) {
      tool_contact_status_interface_ = *it;
      if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_STANDBY)) {
        RCLCPP_ERROR(get_node()->get_logger(),
                     "Failed to set '%s' command interface, aborting activation of controller.",
                     interface_name.c_str());
        return controller_interface::CallbackReturn::ERROR;
      }
    } else {
      RCLCPP_ERROR(get_node()->get_logger(), "Did not find '%s' in command interfaces.", interface_name.c_str());
      return controller_interface::CallbackReturn::ERROR;
    }
  }
  {
    const std::string interface_name = tool_contact_params_.tf_prefix + "tool_contact/tool_contact_result";
    auto it = std::find_if(state_interfaces_.begin(), state_interfaces_.end(),
                           [&](auto& interface) { return (interface.get_name() == interface_name); });
    if (it != state_interfaces_.end()) {
      tool_contact_result_interface_ = *it;
      if (!tool_contact_result_interface_->get().get_value()) {
        RCLCPP_ERROR(get_node()->get_logger(),
                     "Failed to read '%s' state interface, aborting activation of controller.", interface_name.c_str());
        return controller_interface::CallbackReturn::ERROR;
      }
    } else {
      RCLCPP_ERROR(get_node()->get_logger(), "Did not find '%s' in state interfaces.", interface_name.c_str());
      return controller_interface::CallbackReturn::ERROR;
    }
  }
  {
    const std::string interface_name = tool_contact_params_.tf_prefix + "get_robot_software_version/get_version_major";
    auto it = std::find_if(state_interfaces_.begin(), state_interfaces_.end(),
                           [&](auto& interface) { return (interface.get_name() == interface_name); });
    if (it != state_interfaces_.end()) {
      tool_contact_version_interface_ = *it;
      if (!tool_contact_result_interface_->get().get_value()) {
        RCLCPP_ERROR(get_node()->get_logger(),
                     "Failed to read '%s' state interface, aborting activation of controller.", interface_name.c_str());
        return controller_interface::CallbackReturn::ERROR;
      }
    } else {
      RCLCPP_ERROR(get_node()->get_logger(), "Did not find '%s' in state interfaces.", interface_name.c_str());
      return controller_interface::CallbackReturn::ERROR;
    }
  }
  if (tool_contact_version_interface_->get().get_value() < 5) {
    RCLCPP_ERROR(get_node()->get_logger(), "This feature is not supported on CB3 robots, controller will not be "
                                           "started.");
    return controller_interface::CallbackReturn::ERROR;
  }

  return controller_interface::CallbackReturn::SUCCESS;
}

controller_interface::CallbackReturn
ToolContactController::on_deactivate(const rclcpp_lifecycle::State& /* previous_state */)
{
  // Abort active goal (if any)
  const auto active_goal = *rt_active_goal_.readFromRT();
  if (active_goal) {
    RCLCPP_INFO(get_node()->get_logger(), "Aborting tool contact, as controller has been deactivated.");
    // Mark the current goal as abort
    auto result = std::make_shared<ur_msgs::action::ToolContact::Result>();
    result->result = ur_msgs::action::ToolContact::Result::ABORTED_BY_CONTROLLER;
    active_goal->setAborted(result);
    rt_active_goal_.writeFromNonRT(RealtimeGoalHandlePtr());
  }
  if (tool_contact_active_) {
    tool_contact_active_ = false;
    change_requested_ = false;
    if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_WAITING_END)) {
      failed_update();
    }
  }

  return controller_interface::CallbackReturn::SUCCESS;
}

// controller_interface::CallbackReturn ToolContactController::on_cleanup(const rclcpp_lifecycle::State&
// previous_state){

// }

std::vector<hardware_interface::CommandInterface> ToolContactController::on_export_reference_interfaces()
{
  std::vector<hardware_interface::CommandInterface> reference_interfaces;
  reference_interfaces.reserve(1);
  reference_interfaces.push_back(
      hardware_interface::CommandInterface(get_node()->get_name(), "tool_contact/enable", &tool_contact_enable));

  return reference_interfaces;
}

std::vector<hardware_interface::StateInterface> ToolContactController::on_export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> state_interfaces;
  state_interfaces.push_back(
      hardware_interface::StateInterface(get_node()->get_name(), "tool_contact/active", &tool_contact_active));

  return state_interfaces;
}

bool ToolContactController::on_set_chained_mode(bool chained_mode)
{
  /* Abort action, if one is running */
  const auto active_goal = *rt_active_goal_.readFromNonRT();
  if (active_goal) {
    RCLCPP_WARN(get_node()->get_logger(), "Aborting tool contact action, as controller is entering chained mode.");
    auto result = std::make_shared<ur_msgs::action::ToolContact::Result>();
    result->result = ur_msgs::action::ToolContact::Result::ABORTED_BY_CONTROLLER;
    active_goal->setAborted(result);
    rt_active_goal_.writeFromNonRT(RealtimeGoalHandlePtr());
  }
  if (chained_mode) {
    RCLCPP_INFO(get_node()->get_logger(), "Entering chained mode, enabling tool contact.");
    if (!tool_contact_active_) {
      change_requested_ = true;
      tool_contact_active_ = true;
    }
  } else {
    RCLCPP_INFO(get_node()->get_logger(), "Leaving chained mode, disabling tool contact.");
    if (!tool_contact_active_) {
      change_requested_ = true;
      tool_contact_active_ = false;
    }
  }
  return true;
}

rclcpp_action::GoalResponse ToolContactController::goal_received_callback(
    const rclcpp_action::GoalUUID& /*uuid*/, std::shared_ptr<const ur_msgs::action::ToolContact::Goal> /* goal */)
{
  RCLCPP_INFO(get_node()->get_logger(), "New goal received.");

  if (get_lifecycle_state().id() != lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) {
    RCLCPP_ERROR(get_node()->get_logger(), "Tool contact controller is not in active state, can not accept action "
                                           "goals.");
    return rclcpp_action::GoalResponse::REJECT;
  }
  if (is_in_chained_mode()) {
    RCLCPP_ERROR(get_node()->get_logger(), "Tool contact controller is in chained mode, can not accept action goals.");
    return rclcpp_action::GoalResponse::REJECT;
  }
  if (tool_contact_active_) {
    RCLCPP_ERROR(get_node()->get_logger(), "Tool contact already active, rejecting goal.");
    return rclcpp_action::GoalResponse::REJECT;
  }
  return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
}

void ToolContactController::goal_accepted_callback(
    std::shared_ptr<rclcpp_action::ServerGoalHandle<ur_msgs::action::ToolContact>> goal_handle)
{
  RCLCPP_INFO(get_node()->get_logger(), "Goal accepted.");
  tool_contact_active_ = true;
  change_requested_ = true;
  RealtimeGoalHandlePtr rt_goal = std::make_shared<RealtimeGoalHandle>(goal_handle);
  rt_goal->execute();
  rt_active_goal_.writeFromNonRT(rt_goal);
  goal_handle_timer_.reset();
  goal_handle_timer_ = get_node()->create_wall_timer(action_monitor_period_.to_chrono<std::chrono::nanoseconds>(),
                                                     std::bind(&RealtimeGoalHandle::runNonRealtime, rt_goal));
  return;
}

rclcpp_action::CancelResponse ToolContactController::goal_cancelled_callback(
    const std::shared_ptr<rclcpp_action::ServerGoalHandle<ur_msgs::action::ToolContact>> goal_handle)
{
  // Check that cancel request refers to currently active goal (if any)
  const auto active_goal = *rt_active_goal_.readFromNonRT();
  if (active_goal && active_goal->gh_ == goal_handle) {
    RCLCPP_INFO(get_node()->get_logger(), "Cancel tool contact requested.");

    // Mark the current goal as canceled
    auto result = std::make_shared<ur_msgs::action::ToolContact::Result>();
    result->result = ur_msgs::action::ToolContact::Result::CANCELLED_BY_USER;
    active_goal->setCanceled(result);
    rt_active_goal_.writeFromNonRT(RealtimeGoalHandlePtr());
    tool_contact_active_ = false;
    change_requested_ = true;
  }

  return rclcpp_action::CancelResponse::ACCEPT;
}

controller_interface::return_type ToolContactController::update_reference_from_subscribers(
    const rclcpp::Time& /* time */, const rclcpp::Duration& /* period */)
{
  if (change_requested_) {
    reference_interfaces_[0] = static_cast<double>(tool_contact_active_);
  }
  return controller_interface::return_type::OK;
}

void ToolContactController::failed_update()
{
  RCLCPP_FATAL(get_node()->get_logger(), "Controller failed to update or read command/state interface.");
  return;
}

controller_interface::return_type ToolContactController::update_and_write_commands(const rclcpp::Time& /* time */,
                                                                                   const rclcpp::Duration& /* period */)
{
  if (!is_in_chained_mode()) {
    if (change_requested_) {
      change_requested_ = false;
      if (reference_interfaces_[0] == 1.0) {
        if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_WAITING_BEGIN)) {
          failed_update();
        }
      } else if (reference_interfaces_[0] == 0.0) {
        if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_WAITING_END)) {
          failed_update();
        }
      }
    }
  } else {
    if (reference_interfaces_[0] != old_reference_val) {
      if (reference_interfaces_[0] == 1.0) {
        if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_WAITING_BEGIN)) {
          failed_update();
        }
      } else if (reference_interfaces_[0] == 0.0) {
        if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_WAITING_END)) {
          failed_update();
        }
      }
    }
  }
  const auto active_goal = *rt_active_goal_.readFromRT();

  const int status = static_cast<int>(tool_contact_status_interface_->get().get_value());
  switch (status) {
    case static_cast<int>(TOOL_CONTACT_EXECUTING):
    {
      if (!logged_once_) {
        RCLCPP_INFO(get_node()->get_logger(), "Tool contact enabled successfully.");
        logged_once_ = true;
      }
      double result = tool_contact_result_interface_->get().get_value();
      if (result == 0.0) {
        RCLCPP_INFO(get_node()->get_logger(), "Tool contact finished successfully.");

        if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_STANDBY)) {
          failed_update();
        }

        if (active_goal) {
          auto result = std::make_shared<ur_msgs::action::ToolContact::Result>();
          result->result = ur_msgs::action::ToolContact::Result::SUCCESS;
          active_goal->setSucceeded(result);
          rt_active_goal_.writeFromNonRT(RealtimeGoalHandlePtr());
          tool_contact_active_ = false;
          change_requested_ = true;
        }
      } else if (result == 1.0) {
        RCLCPP_ERROR(get_node()->get_logger(), "Tool contact aborted by hardware.");

        if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_STANDBY)) {
          failed_update();
        }

        if (active_goal) {
          auto result = std::make_shared<ur_msgs::action::ToolContact::Result>();
          result->result = ur_msgs::action::ToolContact::Result::ABORTED_BY_HARDWARE;
          active_goal->setAborted(result);
          rt_active_goal_.writeFromNonRT(RealtimeGoalHandlePtr());
          tool_contact_active_ = false;
        }
      }
    } break;

    case static_cast<int>(TOOL_CONTACT_FAILURE_BEGIN):
    {
      RCLCPP_ERROR(get_node()->get_logger(), "Tool contact could not be enabled.");

      if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_STANDBY)) {
        failed_update();
      }

      if (active_goal) {
        auto result = std::make_shared<ur_msgs::action::ToolContact::Result>();
        result->result = ur_msgs::action::ToolContact::Result::ABORTED_BY_HARDWARE;
        active_goal->setAborted(result);
        rt_active_goal_.writeFromNonRT(RealtimeGoalHandlePtr());
        tool_contact_active_ = false;
      }
    } break;

    case static_cast<int>(TOOL_CONTACT_SUCCESS_END):
    {
      RCLCPP_INFO(get_node()->get_logger(), "Tool contact disabled successfully.");

      if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_STANDBY)) {
        failed_update();
      }

      tool_contact_active_ = false;
    } break;

    case static_cast<int>(TOOL_CONTACT_FAILURE_END):
    {
      RCLCPP_ERROR(get_node()->get_logger(), "Tool contact could not be disabled.");

      if (!tool_contact_status_interface_->get().set_value(TOOL_CONTACT_STANDBY)) {
        failed_update();
      }

      if (active_goal) {
        auto result = std::make_shared<ur_msgs::action::ToolContact::Result>();
        result->result = ur_msgs::action::ToolContact::Result::ABORTED_BY_HARDWARE;
        active_goal->setAborted(result);
        rt_active_goal_.writeFromNonRT(RealtimeGoalHandlePtr());
      }
    } break;
    case static_cast<int>(TOOL_CONTACT_STANDBY):
      logged_once_ = false;
      break;
    default:
      break;
  }

  old_reference_val = reference_interfaces_[0];
  return controller_interface::return_type::OK;
}

}  // namespace ur_controllers

#include "pluginlib/class_list_macros.hpp"

PLUGINLIB_EXPORT_CLASS(ur_controllers::ToolContactController, controller_interface::ChainableControllerInterface)