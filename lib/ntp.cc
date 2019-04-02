#include "ntp.h"
#include <sys/timex.h>
#include "logger.h"

namespace atlasagent {

double nsec_to_sec(int nsec) { return (double)nsec / 1e9; }

double msec_to_sec(int msec) { return (double)msec / 1e6; }

NTP::NTP(spectator::Registry* registry) noexcept
    : ntpTimeOffset_{registry->GetGauge("sys.time.kernel.offset")},
      ntpEstError_{registry->GetGauge("sys.time.kernel.esterror")},
      ntpPrecision_{registry->GetGauge("sys.time.kernel.precision")},
      ntpUnsynchronized_{registry->GetGauge("sys.time.kernel.unsynchronized")} {}

void NTP::update_stats() noexcept {
  struct timex time = {};

  if (ntp_adjtime(&time) == -1) {
    Logger()->warn("Unable to run ntp_adjtime", strerror(errno));
    return;
  }

  // We don't rely on the result of ntp_adjtime, because TIME_OK
  // is only returned if the time is synchronized *AND* there
  // is no leap second adjustment. Otherwise, if people alerted
  // on this, then the entire fleet would alert as we go into
  // a leap second
  if (time.status & STA_UNSYNC || time.status & STA_CLOCKERR)
    ntpUnsynchronized_->Set(1);
  else
    ntpUnsynchronized_->Set(0);

  if (time.status & STA_NANO)
    ntpTimeOffset_->Set(nsec_to_sec(time.offset));
  else
    ntpTimeOffset_->Set(msec_to_sec(time.offset));

  // These are in microseconds
  ntpEstError_->Set(msec_to_sec(time.esterror));
  ntpPrecision_->Set(msec_to_sec(time.precision));
}

}  // namespace atlasagent