# Digital Smart Clock ⏰
## Monica Chan (mochan@mit.edu)

[demo video] (https://youtu.be/QOQKDGKwMJ4)
## Features Implemented

- HTTP Get Request for the time every 90 seconds. 
- Button on GPIO Pin 45 to switch between displaying seconds/flashing colon without seconds
- Button on GPIO Pin 39 to toggle between the Always On mode and Motion Detecting mode.
- Properly formatting time (not in military time) and including AM/PM. 


## List of Functions (In order as they are in the code, more detailed descriptions can be found in the code)
#### Ones given:
- char_append (used in the http get function to append to the response buffer)
- do_http_GET (performs an http get request)

#### New ones:
- void format_time: the goal is to format the time from military time to non-military time. 
   Arguments:
    - char* response: null terminated character array containing the unformatted time (military time)
    - char* destination: null terminated character array to store the properly formatted time.
- increment_time: This function increments a time string by the elapsed time. 
  Arguments:
    - int milliseconds: containing the time value to increment by in milliseconds.
    - char* reference: null terminated character array containing the time string to be incremented.
    - char* destination: null terminated character array containing the destination for the incremented time.
    - bool military: whether to increment the time in military form or non military time form. 
    
- void http_get_time: Calls the get request and also trims the response buffer to be just the time. 
- bool morning: This function determines whether the time is in the AM or PM and returns a corresponding boolean (1 being AM, 0 being PM)
- void increment_sm: State machine for whether the clock should be getting the time from the http request or from incrementing based on millis() timer information. 
    - IDLE: Default start state, set the timers and immediately move to PING.
    - INCREMENT: Check the time to see if it's been longer than the ping timeout. 
    - PING: Perform a get request to get the new time from the website.
    - Arguments:
        - bool mode: corresponding to whether the colon should be flashing or not, mode = 0 corresponds to colon flashing without seconds, 1 to displaying seconds
        - bool screen_on: corresponding to whether the screen should be on or not(important for imu)
    

- void face_sm: State machine for toggling between the Seconds and the No seconds Colon flashing faces. 
    - START_FACE: Waiting for a down push, if it reads a down push, switch to DOWN_FACE.
    - DOWN_FACE: Waiting for a release, if it reads a release, switch to UP_FACE
    - UP_FACE: Flip whether to display seconds or not, then go back to the start face.
    - Arguments:
        - int button45: Value found from digitalRead for whether button at GPIO pin 45 has been pushed.
    
- void acc_sm: State machine for switching between Screen on vs. off for the accelerometer mode.  
    - START: The start state, defaults to turning the screen on and waiting for 15 seconds or for motion. 
    - MOTION: Switches to this state if there is motion detected, if the imu reads a reading that corresponds to no motion, switches to the stop/increment state. 
    - STOPPED_INCREMENT: switches to this state when there is no motion detected or from the start state. 
    
 
 - void imu_button_sm():  This function represents a state machine for the button at pin 39, responsible for switching between the imu mdoe/ always on mode.
    - IMU_IDLE: Waiting for a button push
    - IMU_DOWN: Button has been pushed; waiting for button release.
    - IMU_UP: Button has been pressed and released, switch between the mode using the always_on boolean.

#### General Structure/Tracing What Happens:
I'm going to trace the BASIC functionality here, then describe the other modes/faces below. 
- Every time the loop function iterates it should check for the accelerometer button (Pin 39).
- Check which mode (always on or imu mode)
- Now I call the increment state machine which I argue is the main state machine of the clock.
    - If you've just plugged in the clock, then the increment state machine should be in Idle mode and immediately call the Ping state, sets some timers inside Ping state if you want to look in the code. 
    - In the Ping state, call the http_get_time(). Then, increment the time by 1 second (I noticed my clock was consistently off 1 second, suspect this may have something to do with the time it takes to do an http request?). Switch state to increment mode. 
    - In the increment mode, first check whether you've exceeded ping timeout. If you haven't, increment the time using increment_time function with the elapsed time, where elapsed time is millis() - time at last http request.
        - In increment_time, nothing too advanced here, just doing some arithmetic/modulo to calculate how much the time needs to be incremented by. I used some character manipulation to get the integer values from the string like:
   	 ```cpp
 	 int response_mins = ((reference[3] - '0') * 10) + (reference[4] - '0');
 	 int response_seconds = ((reference[6] - '0')*10) + (reference[7]-'0');
 	 int response_hours = ((reference[0] - '0') * 10) + (reference[1] - '0');
  	```
  And I used a similar method to switch back to characters after I've incremented the time appropriately. 
    - Still in the increment time mode, If you're in flashing colon mode, check whether you've exceeded the 1 second time for when the colon needs to be on or off, and if its been greater than or equal to one second, flip whether the colon should be on. 
    - In any case, determine whether it is AM or PM by calling the morning() function, then print the final time + whether it is AM/PM. 

##### Other modes!
###### Accelerometer:
- Essentially use another state machine to determine which mode you're in based on button values (Always on or Accelerometer mode). 
- Assuming you're in the Accelerometer mode, find the average acceleration magnitude like we did in Lab 2 for the step counter. 
- At the start state, screen should be on. Set your timers, and wait if it's been more than 15 seconds without motion (switch to the STOPPED_INCREMENT state) , or motion detected (MOTION state).
- If motion detected, wait until motion stops, and if it does stop move to stop increment state.
- In the stop Increment state, check for whether there is motion again, or whether it's been more than 15 seconds. In the latter case, turn off the screen. 

###### Face Switching:
- Use another state machine to determine which face should be displayed using button values. 
- Now for some general notes about the colon flashing mode:
    - I use a boolean to tell the machine whether to display a colon or not, and flip the value of the boolean after 1 second (citing piazza on clearly outlining the duty cycle thing). 
    - This boolean flipping happens with a timer called elapsed_time_colon . 

#### Important Design notes/Thinking
- Basically for each button I had one state machine for the button itself and another state machine for the functionality associated with the button (i.e. for the accelerometer one state machine for detecting a press/release sequence on pin 39 and another state machine for whether the screen should be on or off). 
- If you go through the code you may see response_copy and response_buffer_increment. This is because I use military time to determine whether the clock should be display AM or PM. So basically any time I increment the time displayed, I also increment a military time variable response_buffer_increment. I could probably reduce the number of variables but for time and simplicity sake I just kept these separate. 
    - One of the questions my friends asked me was why I had to increment the military time at all instead of just using the trimmed response buffer (which is in military time). This is a great question and the answer is simply for the edge cases of 11:59PM -> 12:00AM and 11:59 AM -> 12:00 PM. For most other times, just using the response buffer to see whether it is midnight or noon is fine. However, if the server gets pinged at 11:59:30PM, it won't ping the server again until 12:01:00AM, and so the clock will display 12:00:xx PM instead of 12:00:xx AM. 


