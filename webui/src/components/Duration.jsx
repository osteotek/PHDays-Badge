import React from 'react';
import { connect } from 'react-redux';
import { bindActionCreators } from 'redux';
import * as actionCreators from '../store/actions/actionCreators';

const Duration = ({ actions, duration }) => {
  const handleChange = event => {
    if (event.target.value >= 1 && event.target.value <= 20) {
      actions.setDuration(event.target.value);
    }
  };

  const handleUp = () => {
    if (duration < 20) actions.setDuration(duration + 1);
  };
  const handleDown = () => {
    if (duration > 1) actions.setDuration(duration - 1);
  };

  return (
    <div className="duration_base">
      <div className="duration">
        <label htmlFor="duration__input">
          Длительность
          <input
            type="number"
            value={duration}
            onChange={event => {
              handleChange(event);
            }}
            id="duration__input"
          />
        </label>
      </div>
      <div className="duration-controls">
        <button type="button" className="duration-button" onClick={() => handleUp()}>
          +
        </button>
        <button type="button" className="duration-button" onClick={() => handleDown()}>
          -
        </button>
      </div>
    </div>
  );
};

const mapStateToProps = state => ({
  duration: state.present.get('duration')
});

const mapDispatchToProps = dispatch => ({
  actions: bindActionCreators(actionCreators, dispatch)
});

const DurationContainer = connect(mapStateToProps, mapDispatchToProps)(Duration);
export default DurationContainer;
