#ifndef INTERFACE_H
#define INTERFACE_H
#include "stuff.hpp"
#include <GL/gl.h>
#include <string>
#include <vector>
#include <list>
#include <set>

class Framework;

//! mainly responsible for text output (HUD)
/*! This class is to provide a user interface, mainly a HUD. It shows some stuff, like a
    background picture and text messages with some effects. It builds up it's own textured
    bitmap font for that. */
class Interface : public EventReceiver {

public:
	//! unique shown messages
	enum Message {
		//! player has to serve the ball
		YOU_SERVE,
		//! Server hasn't got any client yet
		WAITING_FOR_OPPONENT,
		//! Client tries to connect
		CONNECTING,
		//! the game has started (successful connection)
		FLASH_GAME_STARTED,
		//! the actual player missed a score
		FLASH_YOU_LOST,
		//! the actual player scores
		FLASH_YOU_WIN
	};

	//! The constructor
	/*!	\param control the game's Framework (ie Server or Client) object
	*/
	Interface(Framework* control);
	//! The destructor
	~Interface();
	//! toggle wether the "PAUSED" message is shown
	inline void togglePaused(bool p) { paused = p; };
	//! update the shown round number
	/*! \param r the actual game round
	*/
	void updateRound(int r);
	//! update the shown fps
	/*! \param frames recently achieved Frames Per Second
	*/
	void updateFPS(double frames);

	void updateScore(Side side, int points);

	//! add a Message to be shown
	/*! \param msg the Message type */
	void addMessage(Message msg);
	//! removes a message which should not be shown anymore, if it was before
	/*! \param msg the Message type */
	void removeMessage(Message msg);

	//! process a timer triggered event
	/*!	\param event the event descriptor
	*/
	void action(Event event);

	//! draw the background
	/*! \param cruel wether or not to use cruel stuff to make it look nice for PAUSED mode
	*/
	void drawBackground(bool cruel);
	//! draw the FPS counter onto the screen
	void drawFPS();
	//! draw the round number onto the screen
	void drawRound();
	//! draw the player scores onto the screen
	void drawScore();
	//! draw every active Message onto the screen
	/*! Also draws the PAUSED string if paused. */
	void drawMessages();
private:
	//! a single character in our font
	struct Char {
		//! the bitmap;
		char bitmap[8][8];
		//! the width of this character (height is static)
		int width;
	} character[96];

	//! build up the font
	void createFont();
	//! initialise for overlay drawing
	void beginDraw();
	//! deinitialise overlay drawing
	void endDraw();
	//! helper function to print text
	/*! \param text the text to be written, only to contain chars ranging from 32 to 127
	*/
	void drawText(const std::string& text);
	//! calculate the width including spacing of the given text
	/*! \param text the text to be written, only to contain chars ranging from 32 to 127
	*/
	double textWidth(const std::string& text);

	//! pointer to the game's Framework (ie Server or Client) object
	Framework *framework;
	//! pointer to the first of the GL display lists created per character
	int fontlist;
	//! descriptors for various used textures on the font
	GLuint charTexture[5];
	//! descriptor for the texture used in the background
	GLuint backTexture;
	//! descriptor for the timer used for flashing of messages
	int flashtimer;
	//! actual alpha value of a flashing message
	double flashalpha;

	//! wether or not we're paused
	bool paused;
	//! actual round string
	std::string roundnum;
	//! actual fps string
	std::string fps;
	//! scores to be shown
	std::string score[2];
	//! list of active messages
	std::list<Message> message;
};

#endif
