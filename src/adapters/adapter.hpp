#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <utility>
#include <vector>

template </* Objective */ typename O, /* Parameter */ typename P> class Adapter {
public:
  Adapter() = default;
  virtual ~Adapter() = default;

  explicit Adapter(const Adapter &other) = default;
  auto operator=(const Adapter &other) -> Adapter & = default;
  Adapter(Adapter &&other) noexcept = default;
  auto operator=(Adapter &&other) noexcept -> Adapter & = default;

  auto operator()(O obj, P param) -> P {
    P new_param;

    if (first_update_) {
      first_update_ = false;
      new_param = disturb_param(param);
    } else {
      // Adapt the parameter based on the current and last objective and parameter
      new_param = adapt(obj, last_obj_, param, last_param_);
    }

    if (recording_history_)
      // Record the history of objectives and parameters
      history_.emplace_back(obj, new_param);

    // Update the last objective and parameter for the next call
    last_obj_ = obj;
    last_param_ = param;

    return new_param;
  }

  /**
   * @brief Get the history of objectives and parameters.
   *
   * @return A vector of pairs containing the objective and parameter history.
   */
  auto history() const -> const std::vector<std::pair<O, P>> & { return history_; }
  /**
   * @brief Clear the history of objectives and parameters.
   */
  void clear_history() { history_.clear(); }
  /**
   * @brief Save the history of objectives and parameters as CSV to the specified path.
   */
  void save_history(const std::filesystem::path &&path) const {
    // Create parent directories if they do not exist
    if (!std::filesystem::exists(path.parent_path()))
      std::filesystem::create_directories(path.parent_path());

    // Open the file for writing
    std::ofstream file(path);
    if (!file.is_open())
      throw std::runtime_error("Failed to open file for writing: " + path.string());

    // Write the header
    file << "objective,parameter\n";
    // Write the history
    for (const auto &[obj, param] : history_)
      file << std::format("{},{}\n", obj, param);

    file.close();
  }

  void start_recording_history() {
    recording_history_ = true;
    history_.clear(); // Clear previous history
  }
  void stop_recording_history() { recording_history_ = false; }

protected:
  /**
   * @brief Disturb the parameter on first update.
   *
   * @return The disturbed parameter.
   */
  virtual auto disturb_param(const P &param) -> P = 0;

  /**
   * @brief Adapt the parameter based on the current and last objective and parameter.
   *
   * @return The adapted parameter.
   */
  virtual auto adapt(const O &obj, const O &last_obj, const P &param, const P &last_param) -> P = 0;

private:
  O last_obj_;
  P last_param_;

  bool recording_history_ = false; // Whether to record history

  std::vector<std::pair</* obj */ O, /* param */ P>> history_;

  bool first_update_ = true;
};
