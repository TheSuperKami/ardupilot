/*
 * This file is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Code by Charlie Johnson
 */

#include "AP_BattMonitor_config.h"

#if AP_BATTERY_FUELLEVEL_ANALOG_ENABLED

#include <AP_HAL/AP_HAL.h>
#include <AP_Math/AP_Math.h>

#include "AP_BattMonitor_FuelLevel_Analog.h"

extern const AP_HAL::HAL& hal;

const AP_Param::GroupInfo AP_BattMonitor_FuelLevel_Analog::var_info[] = {

    // @Param: FL_VLT_MIN
    // @DisplayName: Empty fuel level voltage
    // @Description: The voltage seen on the analog pin when the fuel tank is empty. Note: For this type of battery monitor, the voltage seen by the analog pin is displayed as battery voltage on a GCS.
    // @Range: 0.01 10
    // @Units: V
    // @User: Advanced
    AP_GROUPINFO("FL_VLT_MIN", 40, AP_BattMonitor_FuelLevel_Analog, _fuel_level_empty_voltage, 0.5),

    // @Param: FL_V_MULT
    // @DisplayName: Fuel level voltage multiplier
    // @Description: Voltage multiplier to determine what the full tank voltage reading is. This is calculated as 1 / (Voltage_Full - Voltage_Empty) Note: For this type of battery monitor, the voltage seen by the analog pin is displayed as battery voltage on a GCS.
    // @Range: 0.01 10
    // @User: Advanced
    AP_GROUPINFO("FL_V_MULT", 41, AP_BattMonitor_FuelLevel_Analog, _fuel_level_voltage_mult, 0.5),

    // @Param: FL_FLTR
    // @DisplayName: Fuel level filter frequency
    // @Description: Filter frequency in Hertz where a low pass filter is used. This is used to filter out tank slosh from the fuel level reading. A value of -1 disables the filter and unfiltered voltage is used to determine the fuel level. The suggested values at in the range of 0.2 Hz to 0.5 Hz.
    // @Range: -1 1
    // @User: Advanced
    // @Units: Hz
    // @RebootRequired: True
    AP_GROUPINFO("FL_FLTR", 42, AP_BattMonitor_FuelLevel_Analog, _fuel_level_filter_frequency, 0.3),

    // @Param: FL_PIN
    // @DisplayName: Fuel level analog pin number
    // @Description: Analog input pin that fuel level sensor is connected to.Analog Airspeed or RSSI ports can be used for Analog input( some autopilots provide others also). Values for some autopilots are given as examples. Search wiki for "Analog pins".
     // @Values: -1:Disabled, 2:Pixhawk/Pixracer/Navio2/Pixhawk2_PM1, 5:Navigator, 13:Pixhawk2_PM2/CubeOrange_PM2, 14:CubeOrange, 16:Durandal, 100:PX4-v1
    AP_GROUPINFO("FL_PIN", 43, AP_BattMonitor_FuelLevel_Analog, _pin, -1),

    // @Param: FL_FF
    // @DisplayName: First order term
    // @Description: First order polynomial fit term
    // @Range: -10 10
    // @User: Advanced
    AP_GROUPINFO("FL_FF", 45, AP_BattMonitor_FuelLevel_Analog, _fuel_fit_first_order_coeff, 1),

    // @Param: FL_FS
    // @DisplayName: Second order term
    // @Description: Second order polynomial fit term
    // @Range: -10 10
    // @User: Advanced
    AP_GROUPINFO("FL_FS", 46, AP_BattMonitor_FuelLevel_Analog, _fuel_fit_second_order_coeff, 0),

    // @Param: FL_FT
    // @DisplayName: Third order term
    // @Description: Third order polynomial fit term
    // @Range: -10 10
    // @User: Advanced
    AP_GROUPINFO("FL_FT", 47, AP_BattMonitor_FuelLevel_Analog, _fuel_fit_third_order_coeff, 0),

    // @Param: FL_OFF
    // @DisplayName: Offset term
    // @Description: Offset polynomial fit term
    // @Range: -10 10
    // @User: Advanced
    AP_GROUPINFO("FL_OFF", 48, AP_BattMonitor_FuelLevel_Analog, _fuel_fit_offset, 0),    

    // CHECK/UPDATE INDEX TABLE IN AP_BattMonitor_Backend.cpp WHEN CHANGING OR ADDING PARAMETERS

    AP_GROUPEND
};

// Constructor
AP_BattMonitor_FuelLevel_Analog::AP_BattMonitor_FuelLevel_Analog(AP_BattMonitor &mon, AP_BattMonitor::BattMonitor_State &mon_state, AP_BattMonitor_Params &params) :
    AP_BattMonitor_Backend(mon, mon_state, params)
{
    AP_Param::setup_object_defaults(this, var_info);
    _state.var_info = var_info;

    _analog_source = hal.analogin->channel(_pin);

    // create a low pass filter
    // use a pole at 0.3 Hz if the filter is not being used
    _voltage_filter.set_cutoff_frequency((_fuel_level_filter_frequency >= 0) ? _fuel_level_filter_frequency : 0.3f);
}

/*
  read - read the "voltage" and "current"
*/
void AP_BattMonitor_FuelLevel_Analog::read()
{
    if (_analog_source == nullptr) {
        return;
    }

    if (!_analog_source->set_pin(_pin)) {
        _state.healthy = false;
        return;
    }
    _state.healthy = true;

    // get a dt and a timestamp
    const uint32_t tnow = AP_HAL::micros();
    const uint32_t dt_us = tnow - _state.last_time_micros;

    // get voltage from an ADC pin
    const float raw_voltage = _analog_source->voltage_average();

    // Converting sensor reading to actual volume in tank in Litres (quadratic fit)
    const float voltage = 
        (_fuel_fit_third_order_coeff * raw_voltage * raw_voltage * raw_voltage) +
        (_fuel_fit_second_order_coeff * raw_voltage * raw_voltage) + 
        (_fuel_fit_first_order_coeff * raw_voltage) +
        _fuel_fit_offset;
    
    const float filtered_voltage = _voltage_filter.apply(voltage, float(dt_us) * 1.0e-6f);

    // output the ADC voltage to the voltage field for easier calibration of sensors
    // also output filtered voltage as a measure of tank slosh filtering
    // this could be useful for tuning the LPF
    const float &voltage_used = (_fuel_level_filter_frequency >= 0) ? filtered_voltage : voltage;

    _state.voltage = filtered_voltage;

    // this driver assumes that CAPACITY is set to tank volume in millilitres.
    // _fuel_level_voltage_mult is calculated by the user as 1 / (full_voltage - empty_voltage)
    const float fuel_level_ratio = (voltage_used - _fuel_level_empty_voltage) * _fuel_level_voltage_mult;
    const float fuel_level_used_ratio = 1.0 - fuel_level_ratio;

    // map consumed_mah to consumed millilitres
    _state.consumed_mah = fuel_level_used_ratio * _params._pack_capacity;

    _state.current_amps = 0;   

    // map consumed_wh using fixed voltage of 1
    _state.consumed_wh = _state.consumed_mah;

    // record time
    _state.last_time_micros = tnow;
}

#endif  // AP_BATTERY_FUELLEVEL_ANALOG_ENABLED
