#include "fanuc_hardware/fanuc_hardware_interface.hpp"

#include "hardware_interface/types/hardware_interface_type_values.hpp"



namespace fanuc_hardware {
  FanucHardwareInterface::~FanucHardwareInterface()
  {
    if (socket_created_){
      close(sock_);
    }
  }

  hardware_interface::CallbackReturn FanucHardwareInterface::on_init(const hardware_interface::HardwareInfo& info)
  {
  
    if (hardware_interface::SystemInterface::on_init(info) != CallbackReturn::SUCCESS) {
      return hardware_interface::CallbackReturn::ERROR;
    }

    if (int(info_.joints.size()) != number_of_joints_) {
      RCLCPP_FATAL(get_logger(), "Got %d joints. Expected %d.", int(info_.joints.size()), number_of_joints_);
      return hardware_interface::CallbackReturn::ERROR;
    }

    // Create socket
    if((sock_ = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
    {
      RCLCPP_ERROR(get_logger(), "Socket creation error" );
      return hardware_interface::CallbackReturn::FAILURE;
    }

    state_socket_.sin_family = AF_INET;
    state_socket_.sin_port = htons(state_port_);

    // Convert IP addresses from text to binary form
    if(inet_pton(AF_INET, robot_ip_, &state_socket_.sin_addr) <= 0) 
    {
      RCLCPP_ERROR(get_logger(), "Invalid address / Address not supported");
      return hardware_interface::CallbackReturn::FAILURE;
    }

    // Connect to the server
    if(connect(sock_, (struct sockaddr *)&state_socket_, sizeof(state_socket_)) < 0) 
    {
      RCLCPP_ERROR(get_logger(), "Connection failed");
      return hardware_interface::CallbackReturn::FAILURE;
    }

    socket_created_ = true;

    hw_states_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
    hw_commands_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  hardware_interface::CallbackReturn FanucHardwareInterface::on_configure(const rclcpp_lifecycle::State& previous_state)
  {
    (void)previous_state;

    auto ret = read_joints();

    if (!ret.first){
      return hardware_interface::CallbackReturn::SUCCESS;
    }

    std::vector<float> current_positions = ret.second;

    for (uint i = 0; i < hw_states_.size(); i++)
    {
      hw_states_[i] = current_positions[i];
      hw_commands_[i] = 0;
    }

    RCLCPP_INFO(get_logger(), "Successfully configured!");

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  hardware_interface::CallbackReturn FanucHardwareInterface::on_activate(const rclcpp_lifecycle::State& previous_state)
  {
    (void)previous_state;

    for (uint i = 0; i < hw_states_.size(); i++)
    {
      hw_commands_[i] = hw_states_[i];
    }

    RCLCPP_INFO(get_logger(), "Successfully activated!");

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  hardware_interface::CallbackReturn FanucHardwareInterface::on_deactivate(const rclcpp_lifecycle::State& previous_state)
  {
    (void)previous_state;

    RCLCPP_INFO(get_logger(), "Successfully deactivated!");

    return hardware_interface::CallbackReturn::SUCCESS;
  }

  hardware_interface::return_type FanucHardwareInterface::read(const rclcpp::Time& time, const rclcpp::Duration& period)
  {
    (void)time;
    (void)period;

     auto ret = read_joints();

    if (!ret.first){
      return hardware_interface::return_type::ERROR;
    }

    std::vector<float> current_positions = ret.second;

    for (uint i = 0; i < hw_states_.size(); i++)
    {
      hw_states_[i] = current_positions[i];
    }

    // TODO: Read data from state socket

    return hardware_interface::return_type::OK;
  }

  hardware_interface::return_type FanucHardwareInterface::write(const rclcpp::Time& time, const rclcpp::Duration& period)
  {
    (void)time;
    (void)period;

    // TODO: Write joint traj to move socket

    return hardware_interface::return_type::OK;
  }

  std::vector<hardware_interface::StateInterface> FanucHardwareInterface::export_state_interfaces()
  {
    std::vector<hardware_interface::StateInterface> state_interfaces;
    
    for (uint i = 0; i < info_.joints.size(); i++)
    {
      state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_states_[i]));
    }

    return state_interfaces;
  }

  std::vector<hardware_interface::CommandInterface> FanucHardwareInterface::export_command_interfaces() 
  {
    std::vector<hardware_interface::CommandInterface> command_interfaces;
    
    for (uint i = 0; i < info_.joints.size(); i++)
    {
      command_interfaces.emplace_back(hardware_interface::CommandInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_commands_[i]));
    }

    return command_interfaces;
  }

  rclcpp::Logger FanucHardwareInterface::get_logger() {
    return rclcpp::get_logger("FanucHardwareInterface");
  }

  std::pair<bool, std::vector<float>> FanucHardwareInterface::read_joints(){

    RCLCPP_ERROR(get_logger(), "reading joint states");
    std::vector<float> joint_positions;

    int start = 0;
    float joint_value = 0.0;

    
    char *length_buffer = new char[4];

    // Read message 
    // Read 1 byte to get length


    ssize_t packet = socket_read::read_socket(sock_, length_buffer, 4);

    int packet_length;

    char length_bytes[4] = {length_buffer[3], length_buffer[2], length_buffer[1], length_buffer[0]};

    std::memcpy(&packet_length, length_bytes, sizeof(int));

    if (packet_length != 56 || packet == -1) {
      RCLCPP_ERROR(get_logger(), "Issue with package");
      char *status_buffer = new char[40];

      ssize_t status_packet = socket_read::read_socket(sock_, status_buffer, 40);
      delete[] status_buffer;

      joint_positions = {0, 0, 0, 0, 0, 0};

      return std::make_pair(true, joint_positions);
    }

    delete[] length_buffer;

    char *state_buffer = new char[state_buffer_length_];
    ssize_t read_buffer = socket_read::read_socket(sock_, state_buffer, state_buffer_length_);
    
    // int length = 0;
    // char test_bytes[4] = {state_buffer[3], state_buffer[2], state_buffer[1], state_buffer[0]};
    // std::memcpy(&length, test_bytes, sizeof(int));
    // RCLCPP_INFO_STREAM(get_logger(), "buffer : " << std::to_string(length));
    
    // if(length != 56)
    // {
    //   RCLCPP_ERROR(get_logger(), "Socket message error");
    //   // joint_positions = {0, 0, 0, 0, 0, 0};
    //   // delete[] state_buffer;
    //   // return std::make_pair(true, joint_positions);
    // }

    for ( int i = 0; i < state_buffer_length_; i++) 
    {
      std::cout << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)(unsigned char)state_buffer[i];

      if (i%4==0){
        std::cout << std::endl;
      }
    }


    for (int i=0; i<6; i++){
      start = 20 + (4*i);
      
      char joint_bytes[4] = {state_buffer[start+3], state_buffer[start+2], state_buffer[start+1], state_buffer[start]};

      std::memcpy(&joint_value, joint_bytes, sizeof(float));
      joint_positions.push_back(joint_value);

      RCLCPP_INFO_STREAM(get_logger(), "Joint " << std::to_string(i+1).c_str()  << ": " << std::to_string(joint_value));
    }

    delete[] state_buffer;

    return std::make_pair(true, joint_positions);
  }
}

ssize_t socket_read::read_socket(int __fd, void *__buf, size_t __nbytes) {
  return read(__fd, __buf, __nbytes);
}

#include "pluginlib/class_list_macros.hpp"

PLUGINLIB_EXPORT_CLASS(fanuc_hardware::FanucHardwareInterface, hardware_interface::SystemInterface)